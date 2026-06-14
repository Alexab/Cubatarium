#include "GuiLayout.h"
#include "Gui/Widgets/GuiWidget.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace cutum {

namespace {

GuiRect ClientWithPadding(const GuiRect& area, int padding)
{
    return area.Inset(padding);
}

int ClampColumns(const GuiGridSpec& spec)
{
    return std::max(1, spec.columns);
}

std::vector<int> ComputeColumnWidths(int availableW, int columns, const std::vector<int>& weights)
{
    std::vector<int> out(columns, 0);
    std::vector<int> w(columns, 1);
    for (int i = 0; i < columns; ++i) {
        if (i < static_cast<int>(weights.size()) && weights[i] > 0) {
            w[i] = weights[i];
        }
    }
    int sum = std::accumulate(w.begin(), w.end(), 0);
    if (sum <= 0) {
        sum = columns;
    }
    int used = 0;
    for (int i = 0; i < columns; ++i) {
        out[i] = availableW * w[i] / sum;
        used += out[i];
    }
    int rem = std::max(0, availableW - used);
    for (int i = 0; i < rem; ++i) {
        out[i % columns] += 1;
    }
    return out;
}

struct GridPlacementData {
    GuiRect inner;
    int columns{1};
    std::vector<int> colX;
    std::vector<int> colW;
    std::vector<int> rowH;
    int maxRow{0};
};

GridPlacementData BuildGridPlacementData(const GuiRect& clientArea, const GuiGridSpec& spec,
                                         const std::vector<GuiGridItem>& items)
{
    GridPlacementData d;
    d.columns = ClampColumns(spec);
    d.inner = ClientWithPadding(clientArea, std::max(0, spec.padding));
    const int gapsW = std::max(0, d.columns - 1) * std::max(0, spec.hGap);
    const int availableW = std::max(0, d.inner.w - gapsW);
    d.colW = ComputeColumnWidths(availableW, d.columns, spec.columnWeights);
    d.colX.resize(d.columns, d.inner.x);
    for (int c = 1; c < d.columns; ++c) {
        d.colX[c] = d.colX[c - 1] + d.colW[c - 1] + std::max(0, spec.hGap);
    }

    for (const GuiGridItem& it : items) {
        if (!it.widget || !it.widget->IsVisible()) {
            continue;
        }
        const int row = std::max(0, it.row);
        const int rowSpan = std::max(1, it.rowSpan);
        d.maxRow = std::max(d.maxRow, row + rowSpan - 1);
    }
    d.rowH.assign(d.maxRow + 1, 0);
    std::unordered_map<int, int> pendingSpanHeights;
    for (const GuiGridItem& it : items) {
        if (!it.widget || !it.widget->IsVisible()) {
            continue;
        }
        const int row = std::max(0, it.row);
        const int rowSpan = std::max(1, it.rowSpan);
        const int needed = std::max(it.minH, it.widget->GetPreferredHeight());
        if (rowSpan == 1) {
            d.rowH[row] = std::max(d.rowH[row], needed);
            continue;
        }
        pendingSpanHeights[row * 1000 + rowSpan] =
            std::max(pendingSpanHeights[row * 1000 + rowSpan], needed);
    }
    for (const auto& kv : pendingSpanHeights) {
        const int key = kv.first;
        const int row = key / 1000;
        const int rowSpan = key % 1000;
        int current = 0;
        for (int r = row; r < row + rowSpan && r < static_cast<int>(d.rowH.size()); ++r) {
            current += d.rowH[r];
        }
        if (current >= kv.second) {
            continue;
        }
        int missing = kv.second - current;
        for (int r = row; r < row + rowSpan && r < static_cast<int>(d.rowH.size()); ++r) {
            const int add = missing / rowSpan + ((r - row) < (missing % rowSpan) ? 1 : 0);
            d.rowH[r] += add;
        }
    }
    return d;
}

} // namespace

int UGuiLayout::StackVerticalMeasure(const GuiRect& clientArea, int spacing, int padding,
                                    const std::vector<UGuiWidget*>& children)
{
    int total = padding * 2;
    bool first = true;
    for (UGuiWidget* child : children) {
        if (!child || !child->IsVisible()) {
            continue;
        }
        if (!first) {
            total += spacing;
        }
        first = false;
        total += child->GetPreferredHeight();
    }
    return total;
}

void UGuiLayout::StackVertical(const GuiRect& clientArea, int spacing, int padding,
                             const std::vector<UGuiWidget*>& children)
{
    GuiRect cursor = ClientWithPadding(clientArea, padding);
    for (UGuiWidget* child : children) {
        if (!child || !child->IsVisible()) {
            continue;
        }
        const int childH = child->GetPreferredHeight();
        child->SetBounds({cursor.x, cursor.y, cursor.w, childH});
        cursor.y += childH + spacing;
    }
}

void UGuiLayout::StackHorizontal(const GuiRect& clientArea, int spacing, int padding,
                               const std::vector<UGuiWidget*>& children)
{
    GuiRect cursor = ClientWithPadding(clientArea, padding);
    for (UGuiWidget* child : children) {
        if (!child || !child->IsVisible()) {
            continue;
        }
        const int childW = child->GetPreferredWidth();
        child->SetBounds({cursor.x, cursor.y, childW, cursor.h});
        cursor.x += childW + spacing;
    }
}

void UGuiLayout::AnchorChild(const GuiRect& clientArea, GuiAnchorKind kind, int margin,
                            UGuiWidget* child)
{
    if (!child) {
        return;
    }
    const int pw = child->GetPreferredWidth();
    const int ph = child->GetPreferredHeight();
    GuiRect bounds;
    switch (kind) {
    case GuiAnchorKind::Fill:
        bounds = clientArea.Inset(margin);
        break;
    case GuiAnchorKind::TopCenter:
        bounds = {clientArea.x + (clientArea.w - pw) / 2, clientArea.y + margin, pw, ph};
        break;
    case GuiAnchorKind::Center:
        bounds = {clientArea.x + (clientArea.w - pw) / 2,
                  clientArea.y + (clientArea.h - ph) / 2, pw, ph};
        break;
    case GuiAnchorKind::BottomCenter:
        bounds = {clientArea.x + (clientArea.w - pw) / 2,
                  clientArea.y + clientArea.h - ph - margin, pw, ph};
        break;
    case GuiAnchorKind::TopLeft:
    default:
        bounds = {clientArea.x + margin, clientArea.y + margin, pw, ph};
        break;
    }
    child->SetBounds(bounds);
}

int UGuiLayout::GridMeasure(const GuiRect& clientArea, const GuiGridSpec& spec,
                           const std::vector<GuiGridItem>& items)
{
    const GridPlacementData d = BuildGridPlacementData(clientArea, spec, items);
    const int vGap = std::max(0, spec.vGap);
    const int rows = static_cast<int>(d.rowH.size());
    const int totalRows = std::accumulate(d.rowH.begin(), d.rowH.end(), 0);
    const int totalGaps = std::max(0, rows - 1) * vGap;
    return std::max(0, totalRows + totalGaps + std::max(0, spec.padding) * 2);
}

void UGuiLayout::GridPlace(const GuiRect& clientArea, const GuiGridSpec& spec,
                          const std::vector<GuiGridItem>& items)
{
    const GridPlacementData d = BuildGridPlacementData(clientArea, spec, items);
    const int hGap = std::max(0, spec.hGap);
    const int vGap = std::max(0, spec.vGap);

    std::vector<int> rowY(d.rowH.size(), d.inner.y);
    for (int r = 1; r < static_cast<int>(d.rowH.size()); ++r) {
        rowY[r] = rowY[r - 1] + d.rowH[r - 1] + vGap;
    }

    for (const GuiGridItem& it : items) {
        if (!it.widget || !it.widget->IsVisible()) {
            continue;
        }
        const int col = std::clamp(it.col, 0, d.columns - 1);
        const int row = std::clamp(it.row, 0, std::max(0, static_cast<int>(d.rowH.size()) - 1));
        const int colSpan = std::max(1, it.colSpan);
        const int rowSpan = std::max(1, it.rowSpan);
        const int colEnd = std::min(d.columns - 1, col + colSpan - 1);
        const int rowEnd = std::min(static_cast<int>(d.rowH.size()) - 1, row + rowSpan - 1);

        int w = 0;
        for (int c = col; c <= colEnd; ++c) {
            w += d.colW[c];
        }
        w += std::max(0, colEnd - col) * hGap;
        int h = 0;
        for (int r = row; r <= rowEnd; ++r) {
            h += d.rowH[r];
        }
        h += std::max(0, rowEnd - row) * vGap;
        it.widget->SetBounds({d.colX[col], rowY[row], std::max(0, w), std::max(0, h)});
    }
}

HotbarLayoutResult LayoutHotbarRows(int viewportW, int viewportH, int slotSize, int gap,
                                    int marginBottom)
{
    HotbarLayoutResult result;
    result.totalW = 10 * slotSize + 9 * gap;
    result.startX = (viewportW - result.totalW) / 2;
    result.prefabRowY = viewportH - marginBottom - slotSize;
    result.blockRowY = result.prefabRowY - gap - slotSize;
    return result;
}

} // namespace cutum
