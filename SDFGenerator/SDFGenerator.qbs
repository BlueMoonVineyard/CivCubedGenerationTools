CppApplication {
	files: ["*.cpp", "*.h"]

	cpp.cxxLanguageVersion: "c++20"

	Depends { name: "cpp" }
	Depends { name: "Qt"; submodules: ["gui"] }
}
