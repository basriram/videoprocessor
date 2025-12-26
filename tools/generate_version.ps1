# From https://blog.didenko.com/2013/11/version-inventory.html

Param (
 [String]$Project,
 [String]$GitRoot,
 [String]$HeaderFile="version.h",
 [String]$VerPrefix="https://github.com/basriram/videoprocessor/commit/"
)

Push-Location -LiteralPath $GitRoot


Pop-Location
