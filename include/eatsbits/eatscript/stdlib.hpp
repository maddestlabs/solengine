#ifndef EATS_STDLIB_HPP
#define EATS_STDLIB_HPP

namespace eatsbits::abi {
    class HostRegistry;
}

namespace eatsbits::eatscript {
    class Evaluator;

    /**
     * Registers the Eatscript Standard Library into an Evaluator instance:
     * - Console I/O: print, println, console.log, console.warn, console.error
     * - Math: abs, min, max, clamp, round, ceil, floor, sqrt, pow, log, sin, cos, tan, lerp, etc.
     * - String: str, len, string.upper, string.lower, string.trim, string.split, string.join, etc.
     * - System: sys.version, sys.platform, sys.time, sys.clock, sys.sleep, sys.getenv, sys.cwd
     * - File I/O: fs.exists, fs.read_file, fs.write_file, fs.file_size, fs.list_dir
     */
    void registerStandardLibrary(Evaluator& evaluator, abi::HostRegistry* hostRegistry = nullptr);

    /**
     * Registers standard host functions into a HostRegistry using C-compatible signatures.
     */
    void registerHostStdlib(abi::HostRegistry& hostRegistry);
}

#endif // EATS_STDLIB_HPP
