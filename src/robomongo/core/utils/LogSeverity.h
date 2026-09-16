#pragma once

#include <QMetaType>

// Application log levels keep the existing signal type name without depending
// on MongoDB server internals.
namespace mongo::logger
{
    class LogSeverity
    {
    public:
        constexpr LogSeverity() = default;
        static constexpr LogSeverity Error() { return LogSeverity(Level::Error); }
        static constexpr LogSeverity Warning() { return LogSeverity(Level::Warning); }
        static constexpr LogSeverity Info() { return LogSeverity(Level::Info); }
        static constexpr LogSeverity Log() { return LogSeverity(Level::Debug); }

        constexpr const char *name() const
        {
            switch (_level) {
            case Level::Error: return "Error";
            case Level::Warning: return "Warning";
            case Level::Info: return "Info";
            case Level::Debug: return "Debug";
            }
            return "Info";
        }

        friend constexpr bool operator==(LogSeverity lhs, LogSeverity rhs)
        {
            return lhs._level == rhs._level;
        }

        friend constexpr bool operator!=(LogSeverity lhs, LogSeverity rhs)
        {
            return !(lhs == rhs);
        }

    private:
        enum class Level { Error, Warning, Info, Debug };
        constexpr explicit LogSeverity(Level level) : _level(level) {}
        Level _level = Level::Info;
    };
}

Q_DECLARE_METATYPE(mongo::logger::LogSeverity)
