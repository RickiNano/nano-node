pub fn get_env_bool(variable_name: impl AsRef<str>) -> Option<bool> {
    let variable_name = variable_name.as_ref();
    std::env::var(variable_name)
        .ok()
        .map(|val| match val.to_lowercase().as_ref() {
            "1" | "true" | "on" => true,
            "0" | "false" | "off" => false,
            _ => panic!("Invalid environment boolean value: {variable_name} = {val}"),
        })
}
