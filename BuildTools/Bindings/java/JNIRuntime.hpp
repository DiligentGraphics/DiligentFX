/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

#include <jni.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace DiligentBindings
{

// Unwind C++ temporaries without replacing an exception already pending in Java.
struct JavaException
{};

inline void Check(JNIEnv* Env)
{
    if (Env->ExceptionCheck())
        throw JavaException{};
}

template <typename T>
class LocalRef
{
public:
    LocalRef(JNIEnv* Env, T Value) : m_Env{Env}, m_Value{Value} { Check(Env); }
    ~LocalRef()
    {
        if (m_Value) m_Env->DeleteLocalRef(m_Value);
    }
    LocalRef(const LocalRef&)            = delete;
    LocalRef& operator=(const LocalRef&) = delete;
    T         Get() const { return m_Value; }

private:
    JNIEnv* m_Env;
    T       m_Value;
};

inline void Throw(JNIEnv* Env, const char* ClassName, const char* Message)
{
    if (Env->ExceptionCheck())
        return;
    jclass Class = Env->FindClass(ClassName);
    if (Class)
    {
        Env->ThrowNew(Class, Message);
        Env->DeleteLocalRef(Class);
    }
}

inline void Require(JNIEnv* Env, bool Condition, const char* Message)
{
    if (!Condition)
    {
        Throw(Env, "java/lang/IllegalArgumentException", Message);
        throw JavaException{};
    }
}

inline jfieldID Field(JNIEnv* Env, jobject Object, const char* Name, const char* Signature)
{
    Require(Env, Object != nullptr, "Required object is null");
    const LocalRef<jclass> Class{Env, Env->GetObjectClass(Object)};
    jfieldID               Result = Env->GetFieldID(Class.Get(), Name, Signature);
    Check(Env);
    return Result;
}

inline jobject NewObject(JNIEnv* Env, const char* ClassName, bool Descriptor = false)
{
    const LocalRef<jclass> Class{Env, Env->FindClass(ClassName)};
    jmethodID              Constructor = Env->GetMethodID(Class.Get(), "<init>", Descriptor ? "(Z)V" : "()V");
    Check(Env);
    jobject Result = Descriptor ? Env->NewObject(Class.Get(), Constructor, JNI_FALSE) : Env->NewObject(Class.Get(), Constructor);
    Check(Env);
    return Result;
}

template <typename Native, typename Java>
Native ReadScalar(JNIEnv* Env, Java Value)
{
    if constexpr (std::is_enum<Native>::value)
    {
        using Underlying = typename std::underlying_type<Native>::type;
        return static_cast<Native>(ReadScalar<Underlying>(Env, Value));
    }
    else
    {
        if constexpr (std::is_integral<Native>::value && std::is_integral<Java>::value)
        {
            // Uint64 uses the bits of a Java long; smaller unsigned types use a
            // wider signed Java type so their full numeric range remains available.
            if constexpr (std::is_unsigned<Native>::value && sizeof(Native) < sizeof(Java))
            {
                Require(Env, Value >= 0 && static_cast<std::uint64_t>(Value) <= (std::numeric_limits<Native>::max)(),
                        "Unsigned value is out of range");
            }
        }
        return static_cast<Native>(Value);
    }
}

template <typename Java, typename Native>
Java WriteScalar(Native Value)
{
    if constexpr (std::is_enum<Native>::value)
    {
        using Underlying = typename std::underlying_type<Native>::type;
        return WriteScalar<Java>(static_cast<Underlying>(Value));
    }
    else if constexpr (std::is_integral<Native>::value && std::is_unsigned<Native>::value &&
                       sizeof(Native) == sizeof(Java) && std::is_signed<Java>::value)
    {
        Java Result;
        std::memcpy(&Result, &Value, sizeof(Result));
        return Result;
    }
    else
    {
        return static_cast<Java>(Value);
    }
}

// JNI's *StringUTF functions use modified UTF-8. Native APIs use ordinary UTF-8,
// so convert through UTF-16 explicitly, including supplementary code points.
inline std::string ReadString(JNIEnv* Env, jstring Value)
{
    const jsize Size = Env->GetStringLength(Value);
    Check(Env);
    std::vector<jchar> Chars(static_cast<size_t>(Size));
    if (Size != 0)
        Env->GetStringRegion(Value, 0, Size, Chars.data());
    Check(Env);
    std::string Result;
    for (size_t Index = 0; Index < Chars.size(); ++Index)
    {
        std::uint32_t Code = Chars[Index];
        Require(Env, Code != 0, "Embedded NUL is not valid in a native string");
        if (Code >= 0xD800 && Code <= 0xDBFF)
        {
            Require(Env, Index + 1 < Chars.size() && Chars[Index + 1] >= 0xDC00 && Chars[Index + 1] <= 0xDFFF,
                    "Unpaired UTF-16 surrogate");
            Code = 0x10000 + ((Code - 0xD800) << 10) + (Chars[++Index] - 0xDC00);
        }
        else
        {
            Require(Env, Code < 0xDC00 || Code > 0xDFFF, "Unpaired UTF-16 surrogate");
        }
        if (Code < 0x80)
        {
            Result.push_back(static_cast<char>(Code));
        }
        else
        {
            const unsigned Count = Code < 0x800 ? 2 : Code < 0x10000 ? 3 :
                                                                       4;
            Result.push_back(static_cast<char>((0xFFu << (8 - Count)) | (Code >> (6 * (Count - 1)))));
            for (unsigned Shift = 6 * (Count - 1); Shift != 0;)
            {
                Shift -= 6;
                Result.push_back(static_cast<char>(0x80 | ((Code >> Shift) & 0x3F)));
            }
        }
    }
    return Result;
}

inline jstring WriteString(JNIEnv* Env, const char* Value)
{
    if (!Value)
        return nullptr;
    std::vector<jchar>   Chars;
    const unsigned char* Cursor = reinterpret_cast<const unsigned char*>(Value);
    while (*Cursor)
    {
        std::uint32_t Code = *Cursor++;
        if (Code >= 0x80)
        {
            const unsigned Count = Code >= 0xC2 && Code <= 0xDF ? 2 : Code >= 0xE0 && Code <= 0xEF ? 3 :
                Code >= 0xF0 && Code <= 0xF4                                                       ? 4 :
                                                                                                     0;
            Require(Env, Count != 0, "Invalid native UTF-8");
            Code &= (1u << (7 - Count)) - 1;
            for (unsigned Index = 1; Index < Count; ++Index)
            {
                Require(Env, *Cursor >= 0x80 && *Cursor <= 0xBF, "Invalid native UTF-8");
                Code = (Code << 6) | (*Cursor++ & 0x3F);
            }
            Require(Env, Code >= (Count == 2 ? 0x80u : Count == 3 ? 0x800u :
                                                                    0x10000u) &&
                        Code <= 0x10FFFF && (Code < 0xD800 || Code > 0xDFFF),
                    "Invalid native UTF-8");
        }
        if (Code < 0x10000)
        {
            Chars.push_back(static_cast<jchar>(Code));
        }
        else
        {
            Code -= 0x10000;
            Chars.push_back(static_cast<jchar>(0xD800 + (Code >> 10)));
            Chars.push_back(static_cast<jchar>(0xDC00 + (Code & 0x3FF)));
        }
    }
    Require(Env, Chars.size() <= static_cast<size_t>((std::numeric_limits<jsize>::max)()), "String is too large for Java");
    const jchar Empty  = 0;
    jstring     Result = Env->NewString(Chars.empty() ? &Empty : Chars.data(), static_cast<jsize>(Chars.size()));
    Check(Env);
    return Result;
}

// A JNI call can have several descriptor inputs and outputs alive together.
// Keep small values inline, but bound each large value's use of the JVM's
// native stack, including descriptor defaults and conversion storage.
template <typename T, bool Large = (sizeof(T) > 4096)>
class ValueStorage
{
public:
    T& Get() { return m_Value; }

private:
    T m_Value{};
};

template <typename T>
class ValueStorage<T, true>
{
public:
    T& Get() { return *m_Value; }

private:
    std::unique_ptr<T> m_Value = std::make_unique<T>();
};

template <typename T>
class Owned
{
public:
    T* Ptr = nullptr;
    ~Owned()
    {
        if (Ptr) Ptr->Release();
    }
    Owned()                        = default;
    Owned(const Owned&)            = delete;
    Owned& operator=(const Owned&) = delete;
};

class Monitor
{
public:
    Monitor(JNIEnv* Env, jobject Object) : m_Env{Env}, m_Object{Object}
    {
        if (Env->MonitorEnter(Object) != JNI_OK)
        {
            Check(Env);
            throw std::runtime_error{"Failed to lock native handle"};
        }
    }
    ~Monitor() { m_Env->MonitorExit(m_Object); }

private:
    JNIEnv* m_Env;
    jobject m_Object;
};

template <typename T>
void ReadInterface(JNIEnv* Env, jobject Value, Owned<T>& Result, const char* StateSignature, bool Nullable)
{
    if (!Value)
    {
        Require(Env, Nullable, "Required interface is null");
        return;
    }
    const jfieldID          StateField = Field(Env, Value, "state", StateSignature);
    const LocalRef<jobject> State{Env, Env->GetObjectField(Value, StateField)};
    const jfieldID          PointerField = Field(Env, State.Get(), "pointer", "J");
    const Monitor           Lock{Env, State.Get()};
    const jlong             Pointer = Env->GetLongField(State.Get(), PointerField);
    Check(Env);
    if (Pointer == 0)
    {
        Throw(Env, "java/lang/IllegalStateException", "Native interface is closed");
        throw JavaException{};
    }
    Result.Ptr = reinterpret_cast<T*>(static_cast<std::uintptr_t>(Pointer));
    Result.Ptr->AddRef();
}

// Java wrappers start empty. Publish their owned references only after every
// result has been converted, so any conversion failure releases all outputs.
class Transfers
{
    struct Entry
    {
        jobject  State;
        jfieldID Field;
        void*    Pointer;
        void (*Release)(void*);
    };

public:
    explicit Transfers(JNIEnv* Env) : m_Env{Env} {}
    ~Transfers()
    {
        for (const Entry& Item : m_Entries)
        {
            if (Item.Pointer)
                Item.Release(Item.Pointer);
            m_Env->DeleteLocalRef(Item.State);
        }
    }

    template <typename T>
    jobject Wrap(Owned<T>& Value, const char* ClassName, const char* StateSignature)
    {
        if (!Value.Ptr)
            return nullptr;
        jobject        Result     = NewObject(m_Env, ClassName);
        const jfieldID StateField = Field(m_Env, Result, "state", StateSignature);
        jobject        State      = m_Env->GetObjectField(Result, StateField);
        Check(m_Env);
        const jfieldID PointerField = Field(m_Env, State, "pointer", "J");
        m_Entries.push_back({State, PointerField, Value.Ptr, [](void* Pointer) { static_cast<T*>(Pointer)->Release(); }});
        Value.Ptr = nullptr;
        return Result;
    }

    void Commit()
    {
        for (Entry& Item : m_Entries)
        {
            m_Env->SetLongField(Item.State, Item.Field, static_cast<jlong>(reinterpret_cast<std::uintptr_t>(Item.Pointer)));
            Item.Pointer = nullptr;
        }
    }

private:
    JNIEnv*            m_Env;
    std::vector<Entry> m_Entries;
};

inline jsize ArrayCount(JNIEnv* Env, std::uint64_t Count)
{
    Require(Env, Count <= static_cast<std::uint64_t>((std::numeric_limits<jsize>::max)()), "Array count is too large for Java");
    return static_cast<jsize>(Count);
}

} // namespace DiligentBindings
