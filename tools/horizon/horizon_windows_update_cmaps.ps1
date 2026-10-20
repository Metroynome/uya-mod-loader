$script_version = 5
$url_base = 'https://box.rac-horizon.com/downloads/maps'

# attempts to download a file
# if the file fails to download
# deletes the local file
function Download-Or-Delete {
  param (
    $url,
    $path
  )

  try { Invoke-WebRequest $url -OutFile $path } catch { if ((Test-Path $path)) { Remove-Item $path } }
}

function Download-MapFiles {
  param (
    $url_base,
    $filename
  )

  $files = @(
    @{url = "$($url_base)/$($filename).bg"; path = "$($filename).bg"}
    @{url = "$($url_base)/$($filename).thumb"; path = "$($filename).thumb"}
    @{url = "$($url_base)/$($filename).map"; path = "$($filename).map"}
    @{url = "$($url_base)/$($filename).world"; path = "$($filename).world"}
    @{url = "$($url_base)/$($filename).sound"; path = "$($filename).sound"}
    @{url = "$($url_base)/$($filename).code"; path = "$($filename).code"}
    @{url = "$($url_base)/$($filename).wad"; path = "$($filename).wad"}
  )

  foreach ($f in $files) { 
    $url = $f.url
    $path = [IO.Path]::GetFullPath($f.path)
    Start-Job -ScriptBlock {param($url,$path) try { Invoke-WebRequest $url -OutFile $path } catch { if ((Test-Path $path)) { Remove-Item $path } } } -ArgumentList $url,$path | Out-Null
  }

  # wait until all files are downloaded
  Get-Job|Wait-Job|Out-Null
}

function Update-Maps {
  param (
    $index_filename,
    $game,
    $file_region_ext
  )

  $updates_filenames = [System.Collections.ArrayList]@()
  $updates_mapnames = [System.Collections.ArrayList]@()
  $updates_local_versions = [System.Collections.ArrayList]@()
  $updates_remote_versions = [System.Collections.ArrayList]@()
  $maps_with_update_count = 0
  $maps_already_up_to_date_count = 0

  # force current directory to pwd
  [Environment]::CurrentDirectory = (Get-Location).Path

  # get index
  $index_contents = (Invoke-WebRequest "$($url_base)/$($index_filename)").Content

  # create out directory
  md $game -ea 0 | Out-Null

  # parse index
  # getting list of all custom maps
  ForEach ($line in $($index_contents -split "`n")) {
    if (! $line) { break }

    # line format is filename|mapname|version
    # split entry by | delimiter
    $parts = $line.split('|')
    $filename = $parts[0]
    $mapname = $parts[1]
    $version = [int]$parts[2]

    # try and get local version
    $local_version = -1
    $missing_files = $false
    $path = "$($game)/$($filename).version"

    if (Test-Path "$($path)" -PathType Leaf) {
      $bytes = [System.IO.File]::ReadAllBytes($path)
      $local_version = [bitconverter]::ToInt32($bytes, 0)

      # ensure a .world and/or .wad exists
      if (-not(Test-Path "$($game)/$($filename)$($file_region_ext).world") -and -not(Test-Path "$($game)/$($filename)$($file_region_ext).wad")) {
        $missing_files = $true
      }
    }

    if ($local_version -lt $version -or $missing_files -eq $true) {
      $updates_filenames += $filename
      $updates_mapnames += $mapname
      $updates_local_versions += $local_version
      $updates_remote_versions += $version
      $maps_with_update_count += 1
    } else {
      $maps_already_up_to_date_count += 1
    }
  }

  echo "$($maps_with_update_count) pending updates, $($maps_already_up_to_date_count) up to date"

  if ($maps_with_update_count -gt 0) {
    For ($i=0; $i -lt $maps_with_update_count; $i++) {
      $filename = $updates_filenames[$i]
      $mapname = $updates_mapnames[$i]
      $local_version = $updates_local_versions[$i]
      $remote_version = $updates_remote_versions[$i]
      echo "updating $mapname ($local_version => $remote_version)..."

      # download map files
      Download-MapFiles $url_base "$($game)/$($filename)$($file_region_ext)"
      Download-Or-Delete "$($url_base)/$($game)/$($filename).version" "$($game)/$($filename).version"
    }
    
    # download global version
    Download-Or-Delete "$($url_base)/$($game)/version" "$($game)/version"
  } else {
    echo "There are no pending updates"
  }
}

function Update-Script {
  
  try {
    # get index
    $version_contents = (Invoke-WebRequest "$($url_base)/update_scripts_version.txt").Content
    $remote_version = [int]$version_contents

    if ($remote_version -gt $script_version) {
      echo "Detected newer script $script_version => $remote_version.. updating..."

      # download newer script
      Invoke-WebRequest "$($url_base)/horizon_windows_update_cmaps.ps1" -OutFile $PSCommandPath

      echo "Updated. Please rerun script."
      exit
    }
  } catch { } # discard update on fail
}

Update-Script

$choice = $args[0]
if (-not("$($choice)")) {
  echo "      == Horizon Custom Map Updater == "
  echo "  0:  Update maps for Deadlocked and UYA NTSC"
  echo "  1:  Update maps for Deadlocked and UYA PAL"
  echo "  2:  Update maps for only Deadlocked"
  echo "  3:  Update maps for only UYA NTSC"
  echo "  4:  Update maps for only R&C3 PAL"

  $choice = Read-Host "Enter number"
}

Switch ($choice) {
  0 {
    echo "Checking for new Deadlocked maps..."
    Update-Maps 'index_dl_ntsc.txt' 'dl' ''
    echo ""
    echo "Checking for new UYA NTSC maps..."
    Update-Maps 'index_uya_ntsc.txt' 'uya' ''
  }
  
  1 {
    echo "Checking for new Deadlocked maps..."
    Update-Maps 'index_dl_ntsc.txt' 'dl' ''
    echo ""
    echo "Checking for new R&C3 PAL maps..."
    Update-Maps 'index_uya_pal.txt' 'uya' '.pal'
  }
  
  2 {
    echo "Checking for new Deadlocked maps..."
    Update-Maps 'index_dl_ntsc.txt' 'dl' ''
  }
  
  3 {
    echo "Checking for new UYA NTSC maps..."
    Update-Maps 'index_uya_ntsc.txt' 'uya' ''
  }
  
  4 {
    echo "Checking for new R&C3 PAL maps..."
    Update-Maps 'index_uya_pal.txt' 'uya' '.pal'
  }

  default {
    echo "invalid option"
    exit
  }
}

Write-Host -NoNewLine 'Press any key to exit...';
$null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown');

#Update-Maps 'index_dl_ntsc.txt' 'dl' ''
#Update-Maps 'index_uya_ntsc.txt' 'uya' ''
#Update-Maps 'index_uya_pal.txt' 'uya' '.pal'
