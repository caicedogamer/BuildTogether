#pragma once

#include <string>
#include <variant>
#include <stdexcept>

namespace ep2p {

    // Minimal Result type. Avoids pulling in Geode's result utilities so the core
    // and net layers remain test-friendly.
    template<typename T>
    class Result {
    public:
        static Result ok(T value) {
            Result r;
            r.m_data  = std::move(value);
            r.m_isOk  = true;
            return r;
        }

        static Result err(std::string message) {
            Result r;
            r.m_error = std::move(message);
            r.m_isOk  = false;
            return r;
        }

        bool isOk()  const { return m_isOk; }
        bool isErr() const { return !m_isOk; }
        explicit operator bool() const { return m_isOk; }

        const T& value() const {
            if (!m_isOk) throw std::logic_error("Result::value() called on error Result");
            return m_data;
        }
        T& value() {
            if (!m_isOk) throw std::logic_error("Result::value() called on error Result");
            return m_data;
        }

        const std::string& error() const { return m_error; }

    private:
        Result() = default;
        T           m_data{};
        std::string m_error;
        bool        m_isOk = false;
    };

    // Specialisation for void results (success/failure with no value).
    template<>
    class Result<void> {
    public:
        static Result ok() {
            Result r;
            r.m_isOk = true;
            return r;
        }
        static Result err(std::string message) {
            Result r;
            r.m_error  = std::move(message);
            r.m_isOk   = false;
            return r;
        }
        bool isOk()  const { return m_isOk; }
        bool isErr() const { return !m_isOk; }
        explicit operator bool() const { return m_isOk; }
        const std::string& error() const { return m_error; }

    private:
        Result() = default;
        std::string m_error;
        bool        m_isOk = false;
    };

} // namespace ep2p
