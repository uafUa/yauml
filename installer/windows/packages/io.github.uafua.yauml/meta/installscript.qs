function Component()
{
}

Component.prototype.createOperations = function()
{
    // Install the packaged application files before adding reversible shell
    // integration. Verification builds suppress integration so they can run
    // without changing the developer or CI user's desktop and file handlers.
    component.createOperations();
    if (installer.value("UumlSkipShellIntegration") === "true")
        return;

    if (systemInfo.productType === "windows") {
        var executable = "@TargetDir@/uuml.exe";
        component.addOperation(
            "CreateShortcut",
            executable,
            "@StartMenuDir@/uuml.lnk",
            "workingDirectory=@TargetDir@",
            "description=Start uuml");
        component.addOperation(
            "RegisterFileType",
            "uuml",
            "\"" + executable + "\" \"%1\"",
            "uuml project",
            "application/x-yauml",
            executable + ",0",
            "ProgId=uuml.Project");
    }
}
