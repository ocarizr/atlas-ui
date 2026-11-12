local execute = model.action("execute")

return {
    start = function()
        model.subscribe(execute, "run_command")
    end,
    run_command = function(command)
        native.call("editor.command", command)
    end,
}
