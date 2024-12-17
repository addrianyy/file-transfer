add_subdirectory(protocol)

target_sources(ft PUBLIC
    Framing.cpp
    Framing.hpp
    Connection.cpp
    Connection.hpp
    Port.cpp
    Port.hpp
)