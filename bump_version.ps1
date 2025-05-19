# Get latest tag version (assuming it starts with "v")
$latest = git describe --tags --abbrev=0
if (-not $latest) {
    Write-Error "No tags found in the repository."
    exit 1
}

# Remove 'v' prefix and split into major/minor/patch
$version = $latest -replace '^v', ''
$parts = $version -split '\.'
$major = [int]$parts[0]
$minor = [int]$parts[1]
$patch = [int]$parts[2]

function Increment-Version {
    param (
        [string]$level
    )

    switch ($level) {
        "patch" {
            $script:patch += 1
        }
        "minor" {
            $script:minor += 1
            $script:patch = 0
        }
        "major" {
            $script:major += 1
            $script:minor = 0
            $script:patch = 0
        }
        default {
            Write-Error "Usage: script.ps1 [major|minor|patch]"
            exit 1
        }
    }
}

# Bump the version based on the first argument
if ($args.Count -lt 1) {
    Write-Error "Usage: script.ps1 [major|minor|patch]"
    exit 1
}

Increment-Version $args[0]

# Find the next unused version tag
do {
    $newVersion = "v$major.$minor.$patch"
    $tagExists = git rev-parse $newVersion 2>$null
    if ($LASTEXITCODE -eq 0) {
        Increment-Version $args[0]
    }
} while ($LASTEXITCODE -eq 0)

# Tag it
git tag $newVersion
Write-Host "Tagged new version: $newVersion"