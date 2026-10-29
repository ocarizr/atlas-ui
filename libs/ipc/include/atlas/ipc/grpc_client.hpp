#pragma once

// gRPC 1.84 instantiates its deprecated experimental certificate types inside
// the MSVC STL while parsing credentials.h. Keep this vendor-header diagnostic
// scoped; project call sites still compile with warnings as errors.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
