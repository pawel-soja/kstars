/*
    Copyright (C) 2022 by Pawel Soja <kernel32.pl@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/
#pragma once

#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <type_traits>
#include <utility>

namespace Ekos
{

// is_iterable
template <typename ...>
std::false_type __is_iterable(...) noexcept;

template <typename T>
auto __is_iterable(int) noexcept -> decltype(
    void(   std::begin(std::declval<T>()) != std::end(std::declval<T>())), // test begin, end, operator !=
    void(*++std::begin(std::declval<T>())),                                // test operator *, operator ++
    std::true_type{}
);

template <typename ... T>
using is_iterable = decltype(__is_iterable<T...>(0));

// is_stl_map_like
template <typename ...>
std::false_type __is_stl_map_like(...) noexcept;

template <typename T>
auto __is_stl_map_like(int) -> decltype(
    void(   std::begin(std::declval<T>()) != std::end(std::declval<T>())), // test begin, end, operator !=
    void(*++std::begin(std::declval<T>())),                                // test operator *, operator ++
    void(   std::begin(std::declval<T>())->first),                         // test iterator type
    void(   std::begin(std::declval<T>())->second),                        // test iterator type
    std::true_type{}
);

template <typename T>
using is_stl_map_like = decltype(__is_stl_map_like<T>(0));

// is_qt_map_like
template <typename ...>
std::false_type __is_qt_map_like(...) noexcept;

template <typename T>
auto __is_qt_map_like(int) -> decltype(
    void(   std::begin(std::declval<T>()) != std::end(std::declval<T>())), // test begin, end, operator !=
    void(*++std::begin(std::declval<T>())),                                // test operator *, operator ++
    void(   std::begin(std::declval<T>()).key()),                          // test iterator type
    void(   std::begin(std::declval<T>()).value()),                        // test iterator type
    std::true_type{}
);

template <typename T>
using is_qt_map_like = decltype(__is_qt_map_like<T>(0));

// is_list_like
template <typename ...>
std::false_type __is_list_like(...) noexcept;

template <typename T>
typename std::enable_if<is_iterable<T>::value && !is_stl_map_like<T>::value && !is_qt_map_like<T>::value, std::true_type>::type
__is_list_like(int) noexcept;

template <typename T>
using is_list_like = decltype (__is_list_like<T>(0));

// Qt
template <typename T>
class AsKeyValue
{
    typedef decltype(std::declval<T>().keyValueBegin()) iterator;
    typedef decltype(std::declval<T>().constKeyValueBegin()) const_iterator;
public:

    explicit AsKeyValue(T &data) : mData(data) {}
public:
    iterator        begin() const noexcept { return mData.keyValueBegin(); }
    iterator        end()   const noexcept { return mData.keyValueEnd(); }
    const_iterator cbegin() const noexcept { return mData.constKeyValueBegin(); }
    const_iterator cend()   const noexcept { return mData.constKeyValueEnd(); }

private:
    T &mData;
};

template <typename T>
inline typename std::enable_if<is_qt_map_like<T>::value, AsKeyValue<T>>::type
asKeyValue(T &data) noexcept
{
    return AsKeyValue<T>(data);
}

// forward declaration
template <typename Source>
inline typename std::enable_if<!is_iterable<Source>::value, QVariant>::type
toVariant(const Source &src);

template <typename Source>
inline typename std::enable_if<is_stl_map_like<Source>::value, QVariantMap>::type
toVariant(const Source &src);

template <typename Source>
inline typename std::enable_if<is_qt_map_like<Source>::value, QVariantMap>::type
toVariant(const Source &src);

template <typename Source>
inline typename std::enable_if<is_list_like<Source>::value, QVariantList>::type
toVariant(const Source &src);

// impl.
template <typename Source>
inline typename std::enable_if<!is_iterable<Source>::value, QVariant>::type
toVariant(const Source &src)
{
    return QVariant(src);
}

template <typename Source>
inline typename std::enable_if<is_stl_map_like<Source>::value, QVariantMap>::type
toVariant(const Source &src)
{
    QVariantMap result;
    for (const auto &it: src)
    {
        result[QString::number(it.first)] = toVariant(it.second);
    }
    return result;
}

template <typename Source>
inline typename std::enable_if<is_qt_map_like<Source>::value, QVariantMap>::type
toVariant(const Source &src)
{
    QVariantMap result;
    for (const auto &it: asKeyValue(src))
    {
        result[QString::number(it.first)] = toVariant(it.second);
    }
    return result;
}

template <typename Source>
inline typename std::enable_if<is_list_like<Source>::value, QVariantList>::type
toVariant(const Source &src)
{
    QVariantList result;
    for (const auto &it: src)
    {
        result.append(it);
    }
    return result;
}

inline QVariant variantize(const QVariant &variant)
{
    if (variant.typeName() == QString("QList<int>")) return toVariant(variant.value<QList<int>>());
    if (variant.typeName() == QString("QList<double>")) return toVariant(variant.value<QList<double>>());

    return variant;
}

template <typename T>
inline T& qvariant_ref(QVariant &v)
{
    if (v.data_ptr().type != qMetaTypeId<T>())
        v = T();

    auto& d = v.data_ptr();
    return *reinterpret_cast<T*>(d.is_shared ? d.data.shared->ptr : &d.data.ptr);
}

}
