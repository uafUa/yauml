function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    if (installer.value("YaumlSkipShellIntegration") === "true")
        return;

    if (systemInfo.productType === "windows") {
        component.addOperation(
            "CreateShortcut",
            "@TargetDir@/yauml.exe",
            "@DesktopDir@/yauml.lnk",
            "workingDirectory=@TargetDir@",
            "description=Start yauml");
    }
}
