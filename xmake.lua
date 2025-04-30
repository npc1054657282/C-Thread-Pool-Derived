for _, file in ipairs(os.files("examples/*.c")) do
    local name = path.basename(file)
    target(name)
    set_kind("binary")
    set_rules("mode.debug")
    add_files("src/threadpool.c")
    add_files("src/utils/log.c")
    add_files("examples/" .. name .. ".c")
    set_group("threadpool_examples")
    add_includedirs("src")
    add_syslinks("pthread")
end