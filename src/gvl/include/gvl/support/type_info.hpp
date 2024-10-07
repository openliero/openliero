// Copyright David Abrahams 2002.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
#pragma once

# include <typeinfo>
# include <cstring>

namespace gvl
{

// type ids which represent the same information as std::type_info
// (i.e. the top-level reference and cv-qualifiers are stripped), but
// which works across shared libraries.
struct type_info
{
    inline type_info(std::type_info const& = typeid(void));

    inline bool operator<(type_info const& rhs) const;
    inline bool operator==(type_info const& rhs) const;

    char const* name() const;

 private:
    char const* m_base_type;
};

template <class T>
inline type_info type_id()
{
    return type_info(typeid(T));
}

inline type_info::type_info(std::type_info const& id) : m_base_type(id.name())
{
}

inline bool type_info::operator<(type_info const& rhs) const
{
    return std::strcmp(m_base_type, rhs.m_base_type) < 0;
}

inline bool type_info::operator==(type_info const& rhs) const
{
    return !std::strcmp(m_base_type, rhs.m_base_type);
}

inline char const* type_info::name() const
{
    return m_base_type;
}

}
