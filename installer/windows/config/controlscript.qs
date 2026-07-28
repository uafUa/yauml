/*
 * yauml installer controller.
 *
 * A hybrid installer can update an installation through its maintenance tool,
 * but launching a newer downloaded installer is still a common user path.
 * Make that path deterministic by removing the registered installation first
 * and then installing into the same directory.
 */

function Controller()
{
    this.upgradeAttempted = false;
    this.existingInstallation =
        installer.isInstaller() ? this.findExistingInstallation() : null;
    if (this.existingInstallation) {
        console.log("Existing yauml installation found at "
                    + this.existingInstallation.root);
        // IFW validates TargetDir before entering its first wizard page, so
        // the replacement decision must be completed during initialization.
        // --accept-messages answers this question for verified deployments.
        var answer = QMessageBox.question(
            "YaumlReplaceExisting",
            "Replace existing yauml installation?",
            "An existing yauml installation was found at:\n\n"
                + this.existingInstallation.root
                + "\n\nThe existing application files will be removed before "
                + "the new version is installed. Project files stored "
                + "elsewhere and user preferences will be preserved. "
                + "Continue?",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.Yes);
        if (answer !== QMessageBox.Yes
                || !this.replaceExistingInstallation(true)) {
            installer.setCanceled();
        }
    }
}

Controller.prototype.maintenanceToolAt = function(root)
{
    if (!root)
        return "";
    var normalized = root.replace(/[\\\/]+$/, "");
    var maintenanceTool = normalized + "/maintenancetool.exe";
    return installer.fileExists(maintenanceTool) ? maintenanceTool : "";
}

Controller.prototype.installationFromRoot = function(root)
{
    var maintenanceTool = this.maintenanceToolAt(root);
    if (!maintenanceTool)
        return null;
    return {
        root: root.replace(/[\\\/]+$/, ""),
        maintenanceTool: maintenanceTool
    };
}

Controller.prototype.registryInstallLocations = function()
{
    var locations = [];
    var uninstallRoots = [
        "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    ];
    var productNames = ["yauml", "u" + "uml"];

    for (var rootIndex = 0; rootIndex < uninstallRoots.length; ++rootIndex) {
        for (var nameIndex = 0; nameIndex < productNames.length; ++nameIndex) {
            var search = installer.execute(
                "reg.exe",
                ["query", uninstallRoots[rootIndex], "/s", "/f",
                 productNames[nameIndex], "/d", "/e", "/reg:64"]);
            if (search.length < 2 || search[1] !== 0)
                continue;

            var lines = search[0].split(/\r?\n/);
            for (var lineIndex = 0; lineIndex < lines.length; ++lineIndex) {
                var key = lines[lineIndex].trim();
                if (key.indexOf("HKEY_") !== 0)
                    continue;

                var locationQuery = installer.execute(
                    "reg.exe",
                    ["query", key, "/v", "InstallLocation", "/reg:64"]);
                if (locationQuery.length < 2 || locationQuery[1] !== 0)
                    continue;

                var match = locationQuery[0].match(
                    /InstallLocation\s+REG_\w+\s+([^\r\n]+)/i);
                if (match && locations.indexOf(match[1].trim()) < 0)
                    locations.push(match[1].trim());
            }
        }
    }
    return locations;
}

Controller.prototype.findExistingInstallation = function()
{
    if (systemInfo.productType !== "windows")
        return null;

    // This also honors --root for automated verification and deployment.
    var configured = this.installationFromRoot(installer.value("TargetDir"));
    if (configured)
        return configured;

    // Older installers use a generated uninstall key, so locate them by their
    // registered display name instead of relying on a fixed registry key.
    var registeredLocations = this.registryInstallLocations();
    for (var index = 0; index < registeredLocations.length; ++index) {
        var registered = this.installationFromRoot(
            registeredLocations[index]);
        if (registered)
            return registered;
    }
    return null;
}

Controller.prototype.replaceExistingInstallation = function(showErrors)
{
    if (!this.existingInstallation || this.upgradeAttempted)
        return true;
    this.upgradeAttempted = true;

    var result = installer.execute(
        this.existingInstallation.maintenanceTool,
        ["--accept-messages", "--confirm-command", "purge"]);
    var succeeded = result.length >= 2 && result[1] === 0;
    if (succeeded) {
        // On Windows the maintenance tool delegates removal of its remaining
        // files and target directory to a detached cleanup process. Wait for
        // the whole directory to disappear before reinstalling; checking only
        // components.xml leaves a race in which the old cleanup process can
        // delete files from the new installation.
        var escapedPath =
            this.existingInstallation.root.replace(/'/g, "''");
        var waitScript =
            "$deadline=[DateTime]::UtcNow.AddSeconds(15);"
            + "while((Test-Path -LiteralPath '" + escapedPath + "')"
            + " -and [DateTime]::UtcNow -lt $deadline)"
            + "{Start-Sleep -Milliseconds 100};"
            + "if(Test-Path -LiteralPath '" + escapedPath + "'){exit 1}";
        var waitResult = installer.execute(
            "powershell.exe",
            ["-NoLogo", "-NoProfile", "-NonInteractive",
             "-Command", waitScript]);
        succeeded = waitResult.length >= 2 && waitResult[1] === 0;
    }
    if (!succeeded) {
        var message =
            "The existing yauml installation could not be removed. "
            + "Close yauml and try again.";
        console.log(message);
        if (showErrors) {
            QMessageBox.critical(
                "YaumlUpgradeFailed",
                "Unable to replace yauml",
                message,
                QMessageBox.Ok);
        }
        return false;
    }

    // Preserve custom install locations when continuing with the new package.
    installer.setValue("TargetDir", this.existingInstallation.root);
    this.existingInstallation = null;
    return true;
}
