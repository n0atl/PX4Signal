// resource.h

#pragma once

#include <windows.h>
// px4_drv から独立したプラグインのため、版番号を独自に持つ。
#define VER_FILE	1,0,0,0
#define VER_FILE_STR	"1.0.0"
#define VER_PRODUCT	1,0,0,0
#define VER_PRODUCT_STR	"1.0.0"

#define VER_COMMENTS_STR		""
#define VER_COMPANYNAME_STR		""
#define VER_FILEDESCRIPTION_STR		"TVTest plugin for BonDriver_PX4 signal statistics"
#define VER_INTERNALNAME_STR		"PX4Signal.tvtp"
#define VER_LEGALCOPYRIGHT_STR		""
#define VER_LEGALTRADEMARKS_STR		""
#define VER_ORIGINALFILENAME_STR	VER_INTERNALNAME_STR
#define VER_PRIVATEBUILD_STR		""
#define VER_PRODUCTNAME_STR		"PX4Signal"
#define VER_SPECIALBUILD_STR		""

#if defined(_DEBUG)
#define VER_FLAGS	VS_FF_PRERELEASE | VS_FF_DEBUG
#else
#define VER_FLAGS	0
#endif
