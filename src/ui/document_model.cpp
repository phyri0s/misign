#include "ui/document_model.h"

#include <filesystem>

namespace misign::ui {

DocumentModel::DocumentModel(application::IPdfRenderer &renderer, QObject *parent)
    : QAbstractListModel(parent), m_renderer(renderer)
{
}

std::optional<application::DocumentError> DocumentModel::open(const QString &file)
{
    beginResetModel();
    m_pages.clear();
    const std::optional<application::DocumentError> error =
        m_renderer.open(std::filesystem::path(file.toStdU16String()));
    if (!error) {
        const int count = m_renderer.pageCount();
        m_pages.reserve(static_cast<std::size_t>(count));
        for (int page = 0; page < count; ++page) {
            m_pages.push_back(m_renderer.pageSize(page));
        }
    }
    ++m_generation;
    endResetModel();
    emit generationChanged();
    return error;
}

int DocumentModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_pages.size());
}

QVariant DocumentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const application::PageSize &page = m_pages.at(static_cast<std::size_t>(index.row()));
    switch (role) {
    case PageWidthRole:
        return page.width;
    case PageHeightRole:
        return page.height;
    case HasVisibleAreaRole:
        return !page.isEmpty();
    default:
        return {};
    }
}

QHash<int, QByteArray> DocumentModel::roleNames() const
{
    return {
        {PageWidthRole, "pageWidth"},
        {PageHeightRole, "pageHeight"},
        {HasVisibleAreaRole, "hasVisibleArea"},
    };
}

} // namespace misign::ui
