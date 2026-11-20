#ifndef YOZORA_WASM_BRIDGE_H
#define YOZORA_WASM_BRIDGE_H

#include <JavaScriptCore/JSContextRef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* puts WebAssembly on a page global. slices whose JavaScriptCore has none (armv7) get the core/wasm engine
   behind it, built from core/core/yozora_wasm_api.js at the first read, so a page that never asks pays
   nothing. returns 0 when the global cannot take it */
int yozora_wasm_js_install(JSContextRef context);

#ifdef __cplusplus
}
#endif
#endif
