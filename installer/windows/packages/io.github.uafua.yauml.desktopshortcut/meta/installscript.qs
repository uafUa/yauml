function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    if (installer.value("UumlSkipShellIntegration") === "true")
        return;

    if (systemInfo.productType === "windows") {
        component.addOperation(
            "CreateShortcut",
            "@TargetDir@/uuml.exe",
            "@DesktopDir@/uuml.lnk",
            "workingDirectory=@TargetDir@",
            "description=Start uuml");
    }
}
