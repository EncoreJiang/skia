/*
 * Copyright 2020 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

 #include <iostream>
 #include <errno.h>

 #include "include/core/SkMatrix.h"
 #include "include/core/SkStream.h"
 #include "include/core/SkSurface.h"
 #include "include/encode/SkPngEncoder.h"
 #include "modules/skresources/include/SkResources.h"
 #include "modules/skshaper/utils/FactoryHelpers.h"
 #include "modules/svg/include/SkSVGDOM.h"
 #include "src/utils/SkOSPath.h"
 #include "tools/CodecUtils.h"
 #include "tools/fonts/FontToolUtils.h"
 
 #include "include/ports/SkFontMgr_fontconfig.h"
 #include "include/ports/SkFontScanner_FreeType.h"


static sk_sp<SkFontMgr> fontMgr;
static sk_sp<skresources::ResourceProvider> resourceProvider;

extern "C" void init(const char* resourcePath) {
    fontMgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
    CodecUtils::RegisterAllAvailable();
    auto predecode = skresources::ImageDecodeStrategy::kPreDecode;
    resourceProvider = skresources::DataURIResourceProviderProxy::Make(
            skresources::FileResourceProvider::Make(SkString(resourcePath), predecode),
            predecode,
            fontMgr);
}

extern "C" int render(const void* data, size_t length, const char* output) {
    SkMemoryStream in(data, length, false);
    auto svg_dom = SkSVGDOM::Builder()
                        .setFontManager(fontMgr)
                        .setResourceProvider(resourceProvider)
                        .setTextShapingFactory(SkShapers::BestAvailable())
                        .make(in);


     if (!svg_dom) {
        std::cerr << "Could not parse SVG.\n";
        errno = EINVAL;
        return -1;
    }
    float width = 1024;
    float height = 1024;

    if (svg_dom->getRoot()->getViewBox().has_value()) {
        width = svg_dom->getRoot()->getViewBox()->width();
        height = svg_dom->getRoot()->getViewBox()->height();
    }

    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));

    svg_dom->setContainerSize(SkSize::Make(width, height));
    svg_dom->render(surface->getCanvas());

    SkPixmap pixmap;
    surface->peekPixels(&pixmap);

    SkFILEWStream out(output);
    if (!out.isValid()) {
        std::cerr << "Could not open " << output << " for writing.\n";
        errno = EIO;
        return -1;
    }

    if (!SkPngEncoder::Encode(&out, pixmap, {})) {
        std::cerr << "PNG encoding failed.\n";
        errno = EILSEQ;
        return -1;
    }

    return 0;
}
