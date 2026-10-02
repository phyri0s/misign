#pragma once

#include "application/pdf_renderer.h"

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace misign::ui {

// The pages of the open document, for the page view: one row per page with
// its displayed size in points. Pages are rendered by PageImageProvider.
class DocumentModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    // Changes every time a document is opened or closed: part of the page
    // image URLs, so images of a previous document are never reused.
    Q_PROPERTY(int generation READ generation NOTIFY generationChanged)

public:
    enum Role : std::uint16_t {
        PageWidthRole = Qt::UserRole + 1,
        PageHeightRole,
        HasVisibleAreaRole,
    };

    explicit DocumentModel(application::IPdfRenderer &renderer, QObject *parent = nullptr);

    // Replaces the open document. On failure the model is empty.
    std::optional<application::DocumentError> open(const QString &file);

    [[nodiscard]] int generation() const { return m_generation; }

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void generationChanged();

private:
    application::IPdfRenderer &m_renderer;
    std::vector<application::PageSize> m_pages;
    int m_generation = 0;
};

} // namespace misign::ui
