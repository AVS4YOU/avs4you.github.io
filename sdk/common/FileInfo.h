#ifndef FILEINFO_H_DEFINE
#define FILEINFO_H_DEFINE

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#ifndef COMPONENT_NAME 
#error Uninitialized COMPONENT_NAME define! It should be like as #define COMPONENT_NAME "ComponentName"
#endif

#ifndef DLL_NAME 
#error Uninitialized COMPONENT_NAME define! It should be like as #define COMPONENT_NAME "ComponentName"
#endif

#define COMPANY_NAME				"Ascensio System SIA"
#define LEGAL_COPYRIGHT				"Ascensio System SIA Copyright (c) 2005-2026"

#ifdef RC_X64
#define FILE_DESCRIPTION_ACTIVEX	COMPONENT_NAME " x64 DLL"
#else
#define FILE_DESCRIPTION_ACTIVEX	COMPONENT_NAME " x86 DLL"
#endif
#define	COMPONENT_FILE_NAME_DLL		DLL_NAME ".dll"
#endif //FILEINFO_H_DEFINE
