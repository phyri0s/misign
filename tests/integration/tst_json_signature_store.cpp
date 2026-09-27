#include "adapters/json_signature_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <optional>
#include <string>
#include <vector>

using misign::adapters::JsonSignatureStore;
using misign::application::SavedSignature;
using misign::domain::AffineMatrix;
using misign::domain::InputMode;
using misign::domain::PdfRect;
using misign::domain::Point;
using misign::domain::Signature;
using misign::domain::Stroke;
using namespace std::chrono_literals;

namespace {

// Two strokes with different input modes, pressures and timestamps, and
// coordinates that do not print exactly in decimal, so a round trip that lost
// precision would show.
Signature drawing()
{
    Stroke pen(InputMode::Stylus);
    pen.addPoint(Point(0.1, 0.2, 0.05, 1'234'567'890us));
    pen.addPoint(Point(10.0 / 3.0, 7.7, 0.69, 1'234'572'890us));
    pen.addPoint(Point(20.0, -3.25, 1.0, 1'234'577'890us));
    Stroke mouse(InputMode::Mouse);
    mouse.addPoint(Point(5.5, 12.0, 0.5, 1'235'000'000us));
    mouse.addPoint(Point(30.0, 12.0 + 1e-9, 0.5, 1'235'015'000us));
    Signature signature;
    signature.addStroke(pen);
    signature.addStroke(mouse);
    return signature;
}

// A file as the store writes it, for tests to spoil one field at a time.
QJsonObject validFile()
{
    return QJsonObject{{"format", "misign-signature"},
                       {"version", 1},
                       {"name", "Written by hand"},
                       {"savedAt", "2026-09-27T17:40:00.123Z"},
                       {"strokes", QJsonArray{QJsonObject{
                                       {"inputMode", "stylus"},
                                       {"points", QJsonArray{QJsonArray{1.0, 2.0, 0.5, 1000},
                                                             QJsonArray{3.0, 4.0, 0.6, 6000}}}}}}};
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), contents.size());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Saves drawing() under `name`: the new entry, or one with an empty id when it
// could not be saved, which fails the caller's first check.
SavedSignature saveDrawing(JsonSignatureStore &store, const std::string &name)
{
    return store.save(name, drawing()).value_or(SavedSignature{});
}

QString fileOf(const QString &directory, const SavedSignature &saved)
{
    return QDir(directory).filePath(QString::fromStdString(saved.id) + ".json");
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

std::vector<std::string> names(const std::vector<SavedSignature> &saved)
{
    std::vector<std::string> result;
    result.reserve(saved.size());
    for (const SavedSignature &s : saved) {
        result.push_back(s.name);
    }
    return result;
}

} // namespace

class TestJsonSignatureStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void appliesASavedSignatureInALaterSession();
    void keepsTheNameAsTyped();
    void listsTheMostRecentFirst();
    void renamesWithoutChangingTheDrawingOrTheOrder();
    void removeDeletesTheFile();
    void doesNotSaveAnEmptySignature();
    void rejectsIdsItCouldNotHaveChosen_data();
    void rejectsIdsItCouldNotHaveChosen();
    void leavesOutUnreadableFiles_data();
    void leavesOutUnreadableFiles();
    void writesTheDocumentedFormat();
    void keepsFilesToTheirOwner();
    void defaultDirectoryIsInTheApplicationData();

private:
    std::optional<QTemporaryDir> m_root;
    QString m_rootPath;
    // Inside m_root, created by the first save.
    QString m_directory;
};

void TestJsonSignatureStore::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("Misign"));
    QCoreApplication::setOrganizationName(QStringLiteral("Misign"));
}

void TestJsonSignatureStore::init()
{
    m_root.emplace();
    QVERIFY(m_root->isValid());
    m_rootPath = m_root->path();
    m_directory = QDir(m_rootPath).filePath(QStringLiteral("signatures"));
}

// The ticket's "done when": saved in one session, applied in a later one (a
// new store on the same directory), identical down to the last bit, so it
// fits into a box exactly as the original.
void TestJsonSignatureStore::appliesASavedSignatureInALaterSession()
{
    const Signature original = drawing();
    SavedSignature saved;
    {
        JsonSignatureStore session(m_directory);
        saved = saveDrawing(session, "Signature");
    }
    QVERIFY(!saved.id.empty());

    const JsonSignatureStore later(m_directory);
    const std::vector<SavedSignature> listed = later.list();

    QCOMPARE(listed.size(), 1U);
    QCOMPARE(listed.front().id, saved.id);
    QCOMPARE(listed.front().name, std::string("Signature"));
    QVERIFY(listed.front().signature == original);
    const PdfRect box{100.0, 200.0, 250.0, 260.0};
    QVERIFY(listed.front().signature.fitInto(box) == original.fitInto(box));
}

void TestJsonSignatureStore::keepsTheNameAsTyped()
{
    JsonSignatureStore store(m_directory);
    const std::string name = "Émilie – 署名 \"quoted\" \\ /";

    QVERIFY(store.save(name, drawing()).has_value());

    QCOMPARE(names(JsonSignatureStore(m_directory).list()), std::vector<std::string>{name});
}

void TestJsonSignatureStore::listsTheMostRecentFirst()
{
    JsonSignatureStore store(m_directory);
    for (const char *name : {"first", "second", "third"}) {
        QVERIFY(store.save(name, drawing()).has_value());
        QTest::qWait(5); // savedAt has millisecond resolution.
    }

    QCOMPARE(names(store.list()), (std::vector<std::string>{"third", "second", "first"}));
}

void TestJsonSignatureStore::renamesWithoutChangingTheDrawingOrTheOrder()
{
    JsonSignatureStore store(m_directory);
    const SavedSignature older = saveDrawing(store, "older");
    QTest::qWait(5);
    QVERIFY(!saveDrawing(store, "newer").id.empty());
    QVERIFY(!older.id.empty());

    QVERIFY(store.rename(older.id, "renamed"));

    const std::vector<SavedSignature> listed = JsonSignatureStore(m_directory).list();
    QCOMPARE(names(listed), (std::vector<std::string>{"newer", "renamed"}));
    QCOMPARE(listed.back().id, older.id);
    QVERIFY(listed.back().signature == drawing());
}

void TestJsonSignatureStore::removeDeletesTheFile()
{
    JsonSignatureStore store(m_directory);
    const SavedSignature saved = saveDrawing(store, "to delete");
    QVERIFY(!saved.id.empty());
    const QString file = fileOf(m_directory, saved);
    QVERIFY(QFile::exists(file));

    QVERIFY(store.remove(saved.id));

    QVERIFY(!QFile::exists(file));
    QVERIFY(QDir(m_directory).isEmpty());
    QVERIFY(store.list().empty());
    QVERIFY(!store.remove(saved.id));
    QVERIFY(!store.rename(saved.id, "gone"));
}

void TestJsonSignatureStore::doesNotSaveAnEmptySignature()
{
    JsonSignatureStore store(m_directory);

    QVERIFY(!store.save("empty", Signature{}).has_value());

    QVERIFY(!QDir(m_directory).exists());
}

// An id reaches the file system: anything but a store-chosen id is refused
// before a path is built from it, so it cannot reach outside the directory,
// nor, spelled differently, reach a saved file on a case-insensitive file
// system. In the patterns, `%id` stands for the id of a saved signature and
// `%root` for the directory above the store's (the data function runs before
// init(), which creates it).
void TestJsonSignatureStore::rejectsIdsItCouldNotHaveChosen_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<bool>("upperCase");

    QTest::newRow("empty") << QString() << false;
    QTest::newRow("parent directory") << QStringLiteral("../outside") << false;
    QTest::newRow("absolute path") << QStringLiteral("%root/outside") << false;
    QTest::newRow("saved id with braces") << QStringLiteral("{%id}") << false;
    QTest::newRow("saved id in upper case") << QStringLiteral("%id") << true;
    QTest::newRow("saved id, then a path") << QStringLiteral("%id/../../outside") << false;
}

void TestJsonSignatureStore::rejectsIdsItCouldNotHaveChosen()
{
    QFETCH(QString, pattern);
    QFETCH(bool, upperCase);
    // A victim file where a naive path would land.
    const QString outside = QDir(m_rootPath).filePath(QStringLiteral("outside.json"));
    writeFile(outside, QJsonDocument(validFile()).toJson());
    JsonSignatureStore store(m_directory);
    const SavedSignature saved = saveDrawing(store, "kept");
    QVERIFY(!saved.id.empty());
    QString id = pattern;
    id.replace(QStringLiteral("%id"), QString::fromStdString(saved.id))
        .replace(QStringLiteral("%root"), m_rootPath);
    if (upperCase) {
        id = id.toUpper();
    }

    QVERIFY(!store.rename(id.toStdString(), "renamed"));
    QVERIFY(!store.remove(id.toStdString()));

    QCOMPARE(readFile(outside), QJsonDocument(validFile()).toJson());
    QCOMPARE(names(store.list()), std::vector<std::string>{"kept"});
}

void TestJsonSignatureStore::leavesOutUnreadableFiles_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QByteArray>("contents");

    const auto spoiled = [](const QString &key, const QJsonValue &value) {
        QJsonObject file = validFile();
        file.insert(key, value);
        return QJsonDocument(file).toJson();
    };
    const auto withPoint = [&](const QJsonValue &point) {
        return spoiled("strokes", QJsonArray{QJsonObject{{"inputMode", "stylus"},
                                                         {"points", QJsonArray{point}}}});
    };
    const QString id = newId() + ".json";

    QTest::newRow("damaged") << id << QByteArray(R"({"format": "misign-signature", "vers)");
    QTest::newRow("not an object") << id << QByteArray("[1, 2, 3]");
    QTest::newRow("newer version") << id << spoiled("version", 2);
    QTest::newRow("other format") << id << spoiled("format", "something-else");
    QTest::newRow("name not a string") << id << spoiled("name", 42);
    QTest::newRow("no savedAt") << id << spoiled("savedAt", QJsonValue());
    QTest::newRow("no strokes") << id << spoiled("strokes", QJsonArray());
    QTest::newRow("unknown input mode")
        << id
        << spoiled("strokes",
                   QJsonArray{QJsonObject{{"inputMode", "finger"},
                                          {"points", QJsonArray{QJsonArray{1.0, 2.0, 0.5, 0}}}}});
    QTest::newRow("point too short") << id << withPoint(QJsonArray{1.0, 2.0, 0.5});
    QTest::newRow("point not numbers") << id << withPoint(QJsonArray{"1", 2.0, 0.5, 0});
    QTest::newRow("pressure above 1") << id << withPoint(QJsonArray{1.0, 2.0, 1.5, 0});
    QTest::newRow("timestamp not whole") << id << withPoint(QJsonArray{1.0, 2.0, 0.5, 2.5});
    QTest::newRow("file name not an id") << "notes.json" << QJsonDocument(validFile()).toJson();
}

// Such a file is not listed, and is left as it is: a newer Misign may need it.
void TestJsonSignatureStore::leavesOutUnreadableFiles()
{
    QFETCH(QString, fileName);
    QFETCH(QByteArray, contents);
    JsonSignatureStore store(m_directory);
    QVERIFY(store.save("readable", drawing()).has_value());
    const QString path = QDir(m_directory).filePath(fileName);
    writeFile(path, contents);

    QCOMPARE(names(store.list()), std::vector<std::string>{"readable"});
    QVERIFY(!store.rename(fileName.chopped(5).toStdString(), "renamed"));
    QCOMPARE(readFile(path), contents);
}

// The format is read back by later versions of Misign: it must not change
// without a new version number. Everything but savedAt is compared exactly.
void TestJsonSignatureStore::writesTheDocumentedFormat()
{
    JsonSignatureStore store(m_directory);
    const SavedSignature saved = saveDrawing(store, "Signature");
    QVERIFY(!saved.id.empty());

    QJsonObject file = QJsonDocument::fromJson(readFile(fileOf(m_directory, saved))).object();

    QVERIFY(QDateTime::fromString(file.take("savedAt").toString(), Qt::ISODateWithMs).isValid());
    const QJsonObject expected{
        {"format", "misign-signature"},
        {"version", 1},
        {"name", "Signature"},
        {"strokes",
         QJsonArray{
             QJsonObject{{"inputMode", "stylus"},
                         {"points", QJsonArray{QJsonArray{0.1, 0.2, 0.05, 1'234'567'890},
                                               QJsonArray{10.0 / 3.0, 7.7, 0.69, 1'234'572'890},
                                               QJsonArray{20.0, -3.25, 1.0, 1'234'577'890}}}},
             QJsonObject{
                 {"inputMode", "mouse"},
                 {"points", QJsonArray{QJsonArray{5.5, 12.0, 0.5, 1'235'000'000},
                                       QJsonArray{30.0, 12.0 + 1e-9, 0.5, 1'235'015'000}}}}}}};
    QCOMPARE(file, expected);
}

void TestJsonSignatureStore::keepsFilesToTheirOwner()
{
#ifdef Q_OS_UNIX
    JsonSignatureStore store(m_directory);
    const SavedSignature saved = saveDrawing(store, "private");
    QVERIFY(!saved.id.empty());
    const QString file = fileOf(m_directory, saved);
    constexpr QFileDevice::Permissions kOthers = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                                 QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                                 QFileDevice::WriteOther | QFileDevice::ExeOther;

    QCOMPARE(QFile::permissions(file) & kOthers, QFileDevice::Permissions());
    QCOMPARE(QFile::permissions(m_directory) & kOthers, QFileDevice::Permissions());

    // Rewritten on rename, still private.
    QVERIFY(store.rename(saved.id, "renamed"));
    QCOMPARE(QFile::permissions(file) & kOthers, QFileDevice::Permissions());
#else
    QSKIP("Windows keeps the application data per user; POSIX permissions do not apply");
#endif
}

void TestJsonSignatureStore::defaultDirectoryIsInTheApplicationData()
{
    QCOMPARE(JsonSignatureStore::defaultDirectory(),
             QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                 .filePath(QStringLiteral("signatures")));
    QVERIFY(JsonSignatureStore::defaultDirectory().contains(QStringLiteral("Misign")));
}

QTEST_GUILESS_MAIN(TestJsonSignatureStore)
#include "tst_json_signature_store.moc"
