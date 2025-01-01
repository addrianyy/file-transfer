add_subdirectory(xxhash)

target_sources(ft PUBLIC
    IpAddressFormatter.cpp
    IpAddressFormatter.hpp
    SizeFormatter.cpp
    SizeFormatter.hpp
    TransferTracker.cpp
    TransferTracker.hpp
    Hasher.cpp
    Hasher.hpp
    ByteBuffer.cpp
    ByteBuffer.hpp
)