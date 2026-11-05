# ViewModel authoring content

`atlas::mvvm_content` reads/writes versioned ViewModel XML from/to memory and
generates native C++20 factories using the same independent runtime schema.
It links `atlas::mvvm` and pugixml and never opens application asset paths.

See [XML and factory contracts](../../docs/mvvm.md). The editor persistence service,
streamer acquisition and future packaging pipeline own their respective I/O.
