#ifndef BOARD_D2D1_3_COMPAT_H
#define BOARD_D2D1_3_COMPAT_H

#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <wincodec.h>

typedef enum BoardD2D1ImageSourceLoadingOptions {
  BOARD_D2D1_IMAGE_SOURCE_LOADING_OPTIONS_NONE = 0,
  BOARD_D2D1_IMAGE_SOURCE_LOADING_OPTIONS_RELEASE_SOURCE = 1,
  BOARD_D2D1_IMAGE_SOURCE_LOADING_OPTIONS_CACHE_ON_DEMAND = 2,
  BOARD_D2D1_IMAGE_SOURCE_LOADING_OPTIONS_FORCE_DWORD = 0x7fffffff
} BoardD2D1ImageSourceLoadingOptions;

typedef interface BoardID2D1ImageSourceFromWic BoardID2D1ImageSourceFromWic;
typedef interface BoardID2D1SvgDocument BoardID2D1SvgDocument;
typedef interface BoardID2D1DeviceContext2 BoardID2D1DeviceContext2;
typedef interface BoardID2D1DeviceContext5 BoardID2D1DeviceContext5;

typedef struct BoardID2D1DeviceContext2Vtbl {
  ID2D1DeviceContextVtbl Base;
  void* CreateFilledGeometryRealization;
  void* CreateStrokedGeometryRealization;
  void* DrawGeometryRealization;
  void* CreateInk;
  void* CreateInkStyle;
  void* CreateGradientMesh;
  HRESULT(STDMETHODCALLTYPE* CreateImageSourceFromWic)
  (BoardID2D1DeviceContext2* This, IWICBitmapSource* wicBitmapSource, BoardD2D1ImageSourceLoadingOptions loadingOptions,
   D2D1_ALPHA_MODE alphaMode, BoardID2D1ImageSourceFromWic** imageSource);
  void* CreateLookupTable3D;
  void* CreateImageSourceFromDxgi;
  void* GetGradientMeshWorldBounds;
  void* DrawInk;
  void* DrawGradientMesh;
  void* DrawGdiMetafile;
  void* CreateTransformedImageSource;
} BoardID2D1DeviceContext2Vtbl;

interface BoardID2D1DeviceContext2 {
  const BoardID2D1DeviceContext2Vtbl* lpVtbl;
};

#define BoardID2D1DeviceContext2_CreateImageSourceFromWic(This, wicBitmapSource, loadingOptions, alphaMode,            \
                                                          imageSource)                                                 \
  (This)->lpVtbl->CreateImageSourceFromWic((This), (wicBitmapSource), (loadingOptions), (alphaMode), (imageSource))

typedef struct BoardID2D1DeviceContext5Vtbl {
  BoardID2D1DeviceContext2Vtbl Base;
  void* CreateSpriteBatch;
  void* DrawSpriteBatch;
  void* CreateSvgGlyphStyle;
  void* DrawText;
  void* DrawTextLayout;
  void* DrawColorBitmapGlyphRun;
  void* DrawSvgGlyphRun;
  void* GetColorBitmapGlyphImage;
  void* GetSvgGlyphImage;
  HRESULT(STDMETHODCALLTYPE* CreateSvgDocument)
  (BoardID2D1DeviceContext5* This, IStream* inputXmlStream, D2D1_SIZE_F viewportSize,
   BoardID2D1SvgDocument** svgDocument);
  void(STDMETHODCALLTYPE* DrawSvgDocument)(BoardID2D1DeviceContext5* This, BoardID2D1SvgDocument* svgDocument);
} BoardID2D1DeviceContext5Vtbl;

interface BoardID2D1DeviceContext5 {
  const BoardID2D1DeviceContext5Vtbl* lpVtbl;
};

#define BoardID2D1DeviceContext5_CreateSvgDocument(This, inputXmlStream, viewportSize, svgDocument)                    \
  (This)->lpVtbl->CreateSvgDocument((This), (inputXmlStream), (viewportSize), (svgDocument))
#define BoardID2D1DeviceContext5_DrawSvgDocument(This, svgDocument)                                                    \
  (This)->lpVtbl->DrawSvgDocument((This), (svgDocument))

static const IID kBoardIID_ID2D1DeviceContext2 = {
    0x394ea6a3, 0x0c34, 0x4321, {0x95, 0x0b, 0x6c, 0xa2, 0x0f, 0x0b, 0xe6, 0xc7}};
static const IID kBoardIID_ID2D1DeviceContext5 = {
    0x7836d248, 0x68cc, 0x4df6, {0xb9, 0xe8, 0xde, 0x99, 0x1b, 0xf6, 0x2e, 0xb7}};

#endif
