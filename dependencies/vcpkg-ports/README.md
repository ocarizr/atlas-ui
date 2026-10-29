# Maintained dependency ports

The Protobuf overlay is adapted from [microsoft/vcpkg at the pinned baseline](https://github.com/microsoft/vcpkg/tree/d4c0a46cb8f63e5c9fa007d13baf1cb3e43bf7e3/ports/protobuf) under the accompanying MIT license. Keep the original port patches and metadata aligned with that baseline.

`fix-empty-string-abi.patch` keeps Protobuf's exported empty-string type consistent between its C++17 build and Atlas C++20 consumers. On MSVC Release, the upstream constexpr probe otherwise selects different types and produces an unresolved exported data symbol. Dynamic initialization preserves the existing C++17 ABI; the patch applies to both the runtime and installed public headers.

Recheck this workaround against upstream when updating the dependency baseline. Validate Debug, Release, clang-cl and generated gRPC clients before removing it.
