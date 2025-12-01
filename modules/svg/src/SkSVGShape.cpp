/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "modules/svg/include/SkSVGShape.h"

#include "include/core/SkPaint.h"  // IWYU pragma: keep
#include "include/private/base/SkDebug.h"
#include "modules/svg/include/SkSVGAttribute.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGTypes.h"

class SkSVGNode;
enum class SkSVGTag;

SkSVGShape::SkSVGShape(SkSVGTag t) : INHERITED(t) {}

void SkSVGShape::onRender(const SkSVGRenderContext& ctx) const {
    const auto fillType = ctx.presentationContext().fInherited.fFillRule->asFillType();

    const auto fillPaint = ctx.fillPaint(),
             strokePaint = ctx.strokePaint();

    // Get paint-order, defaulting to fill -> stroke -> markers if not specified
    const auto& paintOrder = ctx.presentationContext().fInherited.fPaintOrder;
    std::array<SkSVGPaintOrder::Component, 3> order = {
        SkSVGPaintOrder::Component::kFill,
        SkSVGPaintOrder::Component::kStroke,
        SkSVGPaintOrder::Component::kMarkers
    };
    
    if (paintOrder.isValue() && paintOrder->type() != SkSVGPaintOrder::Type::kInherit) {
        order = paintOrder->order();
    }

    // Render according to paint-order
    for (const auto& component : order) {
        if (component == SkSVGPaintOrder::Component::kFill && fillPaint.has_value()) {
            this->onDraw(ctx.canvas(), ctx.lengthContext(), *fillPaint, fillType);
        } else if (component == SkSVGPaintOrder::Component::kStroke && strokePaint.has_value()) {
            this->onDraw(ctx.canvas(), ctx.lengthContext(), *strokePaint, fillType);
        }
        // Markers are not handled in SkSVGShape
    }
}

void SkSVGShape::appendChild(sk_sp<SkSVGNode>) {
    SkDEBUGF("cannot append child nodes to an SVG shape.\n");
}
