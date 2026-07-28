function Component()
{
    // Let IFW close a running copy before replacing or removing executable
    // files. The in-app updater normally starts after yauml exits, while this
    // also protects updates started directly from maintenancetool.exe.
    component.addStopProcessForUpdateRequest("yauml.exe");
    component.addStopProcessForUpdateRequest("u" + "uml.exe");
}

Component.prototype.createOperations = function()
{
    // Install the packaged application files before adding reversible shell
    // integration. Verification builds suppress integration so they can run
    // without changing the developer or CI user's desktop and file handlers.
    component.createOperations();
    if (installer.value("YaumlSkipShellIntegration") === "true")
        return;

    if (systemInfo.productType === "windows") {
        var executable = "@TargetDir@/yauml.exe";
        component.addOperation(
            "CreateShortcut",
            executable,
            "@StartMenuDir@/yauml.lnk",
            "workingDirectory=@TargetDir@",
            "description=Start yauml");
        component.addOperation(
            "RegisterFileType",
            "yauml",
            "\"" + executable + "\" \"%1\"",
            "yauml project",
            "application/x-yauml",
            executable + ",0",
            "ProgId=yauml.Project");
        // Keep projects opened through the previous directory suffix working
        // after an in-place application update.
        component.addOperation(
            "RegisterFileType",
            "u" + "uml",
            "\"" + executable + "\" \"%1\"",
            "yauml project",
            "application/x-yauml",
            executable + ",0",
            "ProgId=yauml.LegacyProject");
    }
}
