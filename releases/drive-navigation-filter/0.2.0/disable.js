// Double-click to disable the filter and remove this user's login startup entry.
(function () {
    var files = new ActiveXObject("Scripting.FileSystemObject");
    var shell = new ActiveXObject("WScript.Shell");
    var exe = files.BuildPath(files.GetParentFolderName(WScript.ScriptFullName), "DriveNavigationFilter.exe");
    if (!files.FileExists(exe)) {
        shell.Popup("Missing DriveNavigationFilter.exe", 0, "Drive navigation filter", 16);
        WScript.Quit(1);
    }
    WScript.Quit(shell.Run('"' + exe + '" --disable', 0, true));
}());
