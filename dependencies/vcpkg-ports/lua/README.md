# Lua port overlay

This port is copied from the repository's pinned vcpkg baseline for Lua 5.5.1.
It retains the upstream source hash, patches, build options and `cpp` feature.
The source URL list tries `lua.org` before `www.lua.org`, which avoids an observed
timeout reaching the latter host while retaining the official download origins.

Lua's C++ library uses exception unwinding and C++ API linkage. Atlas links
`unofficial::lua::lua-cpp`; consumers must not substitute the C library silently.
The copied vcpkg port files are covered by [the vcpkg license](license.txt).
