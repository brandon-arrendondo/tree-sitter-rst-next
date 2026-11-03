fn main() {
    let src_dir = std::path::Path::new("src");

    let mut c_config = cc::Build::new();
    c_config.std("c11").include(src_dir);

    #[cfg(target_env = "msvc")]
    c_config.flag("-utf-8");

    let parser_path = src_dir.join("parser.c");
    c_config.file(&parser_path);
    println!("cargo:rerun-if-changed={}", parser_path.to_str().unwrap());

    let scanner_path = src_dir.join("scanner.c");
    if scanner_path.exists() {
        c_config.file(&scanner_path);
        println!("cargo:rerun-if-changed={}", scanner_path.to_str().unwrap());
    }

    // scanner.c #includes the scanner's own sources and headers from this
    // directory; cargo watches every file under a directory it is given.
    let scanner_dir = src_dir.join("tree_sitter_rst");
    if scanner_dir.exists() {
        println!("cargo:rerun-if-changed={}", scanner_dir.to_str().unwrap());
    }

    c_config.compile("tree-sitter-rst");
}
