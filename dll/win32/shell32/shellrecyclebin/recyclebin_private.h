#ifndef _RECYCLEBIN_PRIVATE_H_
#define _RECYCLEBIN_PRIVATE_H_

#include <stdio.h>

#define COBJMACROS

#include <shlobj.h>

#include "recyclebin.h"
#include "recyclebin_v5.h"

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(recyclebin);

/* Defines */

#define RECYCLE_BIN_DIRECTORY_WITH_ACL    L"RECYCLER"
#define RECYCLE_BIN_DIRECTORY_WITHOUT_ACL L"RECYCLED"
#define RECYCLE_BIN_FILE_NAME             L"INFO2"

#define ROUND_UP(N, S) ((( (N) + (S)  - 1) / (S) ) * (S) )

/* Structures on disk */

#include <pshpack1.h>

typedef struct _INFO2_HEADER
{
    DWORD dwVersion;
    DWORD dwNumberOfEntries; /* unused */
    DWORD dwHighestRecordUniqueId; /* unused */
    DWORD dwRecordSize;
    DWORD dwTotalLogicalSize;
} INFO2_HEADER, *PINFO2_HEADER;

#include <poppack.h>

/* Prototypes */

/* recyclebin_generic.c */

HRESULT RecycleBinGeneric_Constructor(OUT IUnknown **ppUnknown);

/* recyclebin_generic_enumerator.c */

EXTERN_C
HRESULT RecycleBinGenericEnum_Constructor(OUT IRecycleBinEnumList **pprbel);

/* recyclebin_v5.c */

HRESULT RecycleBin5_Constructor(IN LPCWSTR VolumePath, OUT IUnknown **ppUnknown);

#endif /* _RECYCLEBIN_PRIVATE_H_ */
