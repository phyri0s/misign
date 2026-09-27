#include "adapters/json_signature_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

namespace misign::adapters {

using application::SavedSignature;
using domain::InputMode;
using domain::Point;
using domain::Signature;
using domain::Stroke;

namespace {

constexpr QLatin1StringView kFormat("misign-signature");
constexpr int kVersion = 1;
constexpr QLatin1StringView kSuffix(".json");
// Doubles hold every integer up to 2^53 exactly: larger timestamps are not ones
// this store wrote.
constexpr double kLargestExactInteger = 9007199254740992.0;

QString inputModeName(InputMode mode)
{
    switch (mode) {
    case InputMode::Stylus:
        return QStringLiteral("stylus");
    case InputMode::Touchpad:
        return QStringLiteral("touchpad");
    case InputMode::Mouse:
        return QStringLiteral("mouse");
    }
    return {};
}

std::optional<InputMode> inputModeFrom(const QString &name)
{
    for (const InputMode mode : {InputMode::Stylus, InputMode::Touchpad, InputMode::Mouse}) {
        if (name == inputModeName(mode)) {
            return mode;
        }
    }
    return std::nullopt;
}

// A saved signature as read from its file, with when it was saved, which
// orders the list.
struct Entry {
    SavedSignature saved;
    QDateTime savedAt;
};

QJsonObject toJson(const std::string &name, const Signature &signature, const QDateTime &savedAt)
{
    QJsonArray strokes;
    for (const Stroke &stroke : signature.strokes()) {
        QJsonArray points;
        for (const Point &point : stroke.points()) {
            points.append(QJsonArray{point.x(), point.y(), point.pressure(),
                                     static_cast<qint64>(point.timestamp().count())});
        }
        strokes.append(QJsonObject{{QStringLiteral("inputMode"), inputModeName(stroke.inputMode())},
                                   {QStringLiteral("points"), points}});
    }
    return QJsonObject{{QStringLiteral("format"), kFormat},
                       {QStringLiteral("version"), kVersion},
                       {QStringLiteral("name"), QString::fromStdString(name)},
                       {QStringLiteral("savedAt"), savedAt.toUTC().toString(Qt::ISODateWithMs)},
                       {QStringLiteral("strokes"), strokes}};
}

// [x, y, pressure, timestamp], none if anything is off: the pressure is
// rejected rather than clamped, as a file this store wrote never has one out
// of range.
std::optional<Point> pointFrom(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 4 || !std::all_of(array.begin(), array.end(),
                                          [](const QJsonValue &v) { return v.isDouble(); })) {
        return std::nullopt;
    }
    const double pressure = array.at(2).toDouble();
    const double timestamp = array.at(3).toDouble();
    if (pressure < 0.0 || pressure > 1.0 || std::trunc(timestamp) != timestamp ||
        std::abs(timestamp) > kLargestExactInteger) {
        return std::nullopt;
    }
    return Point(array.at(0).toDouble(), array.at(1).toDouble(), pressure,
                 std::chrono::microseconds(static_cast<std::int64_t>(timestamp)));
}

std::optional<Stroke> strokeFrom(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const std::optional<InputMode> mode =
        inputModeFrom(object.value(QStringLiteral("inputMode")).toString());
    const QJsonValue points = object.value(QStringLiteral("points"));
    if (!mode || !points.isArray()) {
        return std::nullopt;
    }
    Stroke stroke(*mode);
    for (const auto &p : points.toArray()) {
        const std::optional<Point> point = pointFrom(p);
        if (!point) {
            return std::nullopt;
        }
        stroke.addPoint(*point);
    }
    return stroke;
}

std::optional<Entry> entryFrom(const QByteArray &data, std::string id)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    const QJsonValue name = root.value(QStringLiteral("name"));
    const QDateTime savedAt =
        QDateTime::fromString(root.value(QStringLiteral("savedAt")).toString(), Qt::ISODateWithMs);
    const QJsonValue strokes = root.value(QStringLiteral("strokes"));
    if (root.value(QStringLiteral("format")).toString() != kFormat ||
        root.value(QStringLiteral("version")).toInteger(-1) != kVersion || !name.isString() ||
        !savedAt.isValid() || !strokes.isArray()) {
        return std::nullopt;
    }
    Signature signature;
    for (const auto &s : strokes.toArray()) {
        std::optional<Stroke> stroke = strokeFrom(s);
        if (!stroke) {
            return std::nullopt;
        }
        signature.addStroke(std::move(*stroke));
    }
    if (signature.isEmpty()) {
        return std::nullopt;
    }
    return Entry{{std::move(id), name.toString().toStdString(), std::move(signature)}, savedAt};
}

std::optional<Entry> read(const QString &path, std::string id)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return entryFrom(file.readAll(), std::move(id));
}

// Replaces the file at once: a failed write leaves the previous one intact.
bool write(const QString &path, const QJsonObject &object)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    // Before any data goes in: the signature is never readable by others.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

} // namespace

JsonSignatureStore::JsonSignatureStore(QString directory) : m_directory(std::move(directory)) {}

QString JsonSignatureStore::defaultDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("signatures"));
}

std::vector<SavedSignature> JsonSignatureStore::list() const
{
    std::vector<Entry> entries;
    const QStringList fileNames =
        QDir(m_directory).entryList({QLatin1Char('*') + kSuffix}, QDir::Files);
    for (const QString &fileName : fileNames) {
        std::string id = fileName.chopped(kSuffix.size()).toStdString();
        const std::optional<QString> path = pathOf(id);
        if (!path) {
            continue;
        }
        if (std::optional<Entry> entry = read(*path, std::move(id))) {
            entries.push_back(std::move(*entry));
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.savedAt != b.savedAt ? a.savedAt > b.savedAt : a.saved.id < b.saved.id;
    });

    std::vector<SavedSignature> result;
    result.reserve(entries.size());
    for (Entry &entry : entries) {
        result.push_back(std::move(entry.saved));
    }
    return result;
}

std::optional<SavedSignature> JsonSignatureStore::save(const std::string &name,
                                                       const Signature &signature)
{
    if (signature.isEmpty()) {
        return std::nullopt;
    }
    if (!QDir(m_directory).exists()) {
        if (!QDir().mkpath(m_directory)) {
            return std::nullopt;
        }
        QFile::setPermissions(m_directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ExeOwner);
    }
    std::string id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const std::optional<QString> path = pathOf(id);
    if (!path || !write(*path, toJson(name, signature, QDateTime::currentDateTimeUtc()))) {
        return std::nullopt;
    }
    return SavedSignature{std::move(id), name, signature};
}

bool JsonSignatureStore::rename(const std::string &id, const std::string &name)
{
    const std::optional<QString> path = pathOf(id);
    if (!path) {
        return false;
    }
    const std::optional<Entry> entry = read(*path, id);
    return entry && write(*path, toJson(name, entry->saved.signature, entry->savedAt));
}

bool JsonSignatureStore::remove(const std::string &id)
{
    const std::optional<QString> path = pathOf(id);
    return path && QFile::remove(*path);
}

std::optional<QString> JsonSignatureStore::pathOf(const std::string &id) const
{
    const QString text = QString::fromStdString(id);
    const QUuid uuid = QUuid::fromString(text);
    if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != text) {
        return std::nullopt;
    }
    return QDir(m_directory).filePath(text + kSuffix);
}

} // namespace misign::adapters
