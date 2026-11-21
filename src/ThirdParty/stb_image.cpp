#include "Core/Memory/StbiAllocation.h"
#define STBI_MALLOC(bytes) cutum::StbiAllocate(bytes)
#define STBI_REALLOC(pointer,bytes) cutum::StbiReallocate(pointer,bytes)
#define STBI_FREE(pointer) cutum::StbiFree(pointer)
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb_image.h"
