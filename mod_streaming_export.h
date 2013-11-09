/*******************************************************************************
 mod_streaming_export.h

 Copyright (C) 2009 CodeShop B.V.
 http://www.code-shop.com

 For licensing see the LICENSE file
******************************************************************************/ 

#ifndef MOD_STREAMING_EXPORT_H_AKW
#define MOD_STREAMING_EXPORT_H_AKW

#if defined _WIN32 || defined __CYGWIN__
  #define MOD_STREAMING_DLL_IMPORT __declspec(dllimport)
  #define MOD_STREAMING_DLL_EXPORT __declspec(dllexport)
  #define MOD_STREAMING_DLL_LOCAL
#else
  #if __GNUC__ >= 4
    #define MOD_STREAMING_DLL_IMPORT __attribute__ ((visibility("default")))
    #define MOD_STREAMING_DLL_EXPORT __attribute__ ((visibility("default")))
    #define MOD_STREAMING_DLL_LOCAL  __attribute__ ((visibility("hidden")))
  #else
    #define MOD_STREAMING_DLL_IMPORT
    #define MOD_STREAMING_DLL_EXPORT
    #define MOD_STREAMING_DLL_LOCAL
  #endif
#endif

#endif // MOD_STREAMING_EXPORT_H_AKW

// End Of File






