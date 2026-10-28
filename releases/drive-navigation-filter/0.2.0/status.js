// Double-click for configuration, loader and Explorer filter status.
(function () {
    var files = new ActiveXObject("Scripting.FileSystemObject");
    var shell = new ActiveXObject("WScript.Shell");
    var exe = files.BuildPath(files.GetParentFolderName(WScript.ScriptFullName), "DriveNavigationFilter.exe");
    if (!files.FileExists(exe)) {
        shell.Popup("Missing DriveNavigationFilter.exe", 0, "Drive navigation filter", 16);
        WScript.Quit(1);
    }
    WScript.Quit(shell.Run('"' + exe + '" --status', 0, true));
}());
