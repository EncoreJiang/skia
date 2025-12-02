/*
 * Copyright 2020 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

 #include <iostream>

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

 int main(int argc, char** argv) {
    
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input.svg>\n";
        return 1;
    }
 
    SkFILEStream in(argv[1]);
    if (!in.isValid()) {
        std::cerr << "Could not open " << argv[1] << "\n";
        return 1;
    }
     // If necessary, clients should use a font manager that would load fonts from the system.
     sk_sp<SkFontMgr> fontMgr;
     fontMgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
 
     CodecUtils::RegisterAllAvailable();
 
     auto predecode = skresources::ImageDecodeStrategy::kPreDecode;
     auto rp = skresources::DataURIResourceProviderProxy::Make(
             skresources::FileResourceProvider::Make(SkOSPath::Dirname(argv[1]), predecode),
             predecode,
             fontMgr);
 
     auto svg_dom = SkSVGDOM::Builder()
                            .setFontManager(fontMgr)
                            .setResourceProvider(std::move(rp))
                            .setTextShapingFactory(SkShapers::BestAvailable())
                            .make(in);
 
     if (!svg_dom) {
         std::cerr << "Could not parse " << argv[1] << "\n";
         return 1;
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
 
     SkFILEWStream out("output.png");
     if (!out.isValid()) {
         std::cerr << "Could not open output.png for writing.\n";
         return 1;
     }
 
     // Use default encoding options.
     SkPngEncoder::Options png_options;
 
     if (!SkPngEncoder::Encode(&out, pixmap, png_options)) {
         std::cerr << "PNG encoding failed.\n";
         return 1;
     }
 
     return 0;
 }
 