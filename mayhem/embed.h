#pragma once
#include <stddef.h>

// Releases every buffer the parser took through parser_alloc_buffer since the last call.
void embed_release_all();
