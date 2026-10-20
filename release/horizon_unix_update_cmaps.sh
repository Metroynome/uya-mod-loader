#!/bin/bash

# requires the following dependencies
#   wget (apt install wget)
#   hexdump (apt install bsdmainutils)
version=3
url_base='http://box.rac-horizon.com/downloads/maps'
script_path=$(realpath $0)

# check for new version
remote_version=$(wget -qO- "$url_base/update_scripts_version.txt")
if (( $version < $remote_version )); then
  read -p "Script update available. Would you like to update? (Y/n): "
  if [[ $REPLY == "Y" || $REPLY == "y" ]]; then
    echo "Updating..."
    wget -q -O "$script_path" "$url_base/horizon_unix_update_cmaps.sh"
    echo "Script updated!"
  else
    echo "Please update your script before trying to download new custom maps"
    exit
  fi
fi

# downloads a file in the background from the server
# if it fails it deletes the output file
download_background () {

  url_base=$1
  file=$2

  wget -q -O "$file" "$url_base/$file" || rm -f "$file" &
}

# download map updates for the given game/region
update_maps () {

  index_filename=$1
  game=$2
  file_region_ext=$3

  updates_filenames=()
  updates_mapnames=()
  updates_local_versions=()
  updates_remote_versions=()
  maps_with_update_count=0
  maps_already_up_to_date_count=0

  # get index
  index_contents=$(wget -qO- "$url_base/$index_filename")

  # create out directory
  mkdir -p "$game"

  # parse index
  # getting list of all custom maps
  while read -r line
  do
    # line format is filename|mapname|version
    # split entry by | delimiter
    IFS='|' read -ra part <<< "$line"
    filename="${part[0]}"
    mapname="${part[1]}"
    version="${part[2]}"

    # try and get local version
    local_version=-1
    missing_files=false
    path="$game/$filename.version"
    if [ -f "$path" ]; then
      local_version=$(hexdump -n4 -v -e '/4 "%u\n"' "$path")

      # ensure a .world and/or .wad exists
      if ! [ -f "$game/$filename$file_region_ext.world" ] && ! [ -f "$game/$filename$file_region_ext.wad" ]; then
        missing_files=true
      fi
    fi

    if (( $local_version < $version )) || $missing_files
    then
      updates_filenames+=("$filename")
      updates_mapnames+=("$mapname")
      updates_local_versions+=("$local_version")
      updates_remote_versions+=("$version")
      maps_with_update_count=$(($maps_with_update_count + 1))
    else
      maps_already_up_to_date_count=$(($maps_already_up_to_date_count + 1))
    fi

  done <<< "$index_contents"

  #
  echo "$maps_with_update_count pending updates, $maps_already_up_to_date_count up to date"

  if (( $maps_with_update_count > 0 ))
  then

    for i in $(seq 0 $(($maps_with_update_count - 1)))
    do
      filename=${updates_filenames[$i]}
      mapname=${updates_mapnames[$i]}
      local_version=${updates_local_versions[$i]}
      remote_version=${updates_remote_versions[$i]}
      echo "updating $mapname ($local_version => $remote_version)..."

      # download map
      download_background $url_base "$game/$filename$file_region_ext.bg"
      download_background $url_base "$game/$filename$file_region_ext.map"
      download_background $url_base "$game/$filename$file_region_ext.world"
      download_background $url_base "$game/$filename$file_region_ext.sound"
      download_background $url_base "$game/$filename$file_region_ext.wad"
      download_background $url_base "$game/$filename.version"
      wait

    done

    # download global version
    wget -q -O "$game/version" "$url_base/$game/version"
    echo "$maps_with_update_count updated"
  
  else
    echo "There are no pending updates"
  fi

}

choice=$1

# if no choice provided by command line
# then prompt
if [ -z "$choice" ]; then
  echo "      == Horizon Custom Map Updater == "
  echo "  0:  Update maps for all games"
  echo "  1:  Update maps for Deadlocked and UYA NTSC"
  echo "  2:  Update maps for Deadlocked and UYA PAL"
  echo "  3:  Update maps for UYA NTSC and UYA PAL"
  echo "  4:  Update maps for only Deadlocked"
  echo "  5:  Update maps for only UYA NTSC"
  echo "  6:  Update maps for only R&C3 PAL"
  
  read -p "Enter number: "
  choice=$REPLY
fi

case "$choice" in
  0)
    echo "Checking for new Deadlocked maps..."
    update_maps 'index_dl_ntsc.txt' 'dl' ''
    echo 
    echo "Checking for new UYA NTSC maps..."
    update_maps 'index_uya_ntsc.txt' 'uya' ''
    echo 
    echo "Checking for new R&C3 PAL maps..."
    update_maps 'index_uya_pal.txt' 'uya' '.pal'
    ;;

  1)
    echo "Checking for new Deadlocked maps..."
    update_maps 'index_dl_ntsc.txt' 'dl' ''
    echo 
    echo "Checking for new UYA NTSC maps..."
    update_maps 'index_uya_ntsc.txt' 'uya' ''
    ;;

  2)
    echo "Checking for new Deadlocked maps..."
    update_maps 'index_dl_ntsc.txt' 'dl' ''
    echo 
    echo "Checking for new R&C3 PAL maps..."
    update_maps 'index_uya_pal.txt' 'uya' '.pal'
    ;;

  3)
    echo "Checking for new Deadlocked maps..."
    update_maps 'index_dl_ntsc.txt' 'dl' ''
    echo 
    echo "Checking for new R&C3 PAL maps..."
    update_maps 'index_uya_pal.txt' 'uya' '.pal'
    ;;

  4)
    echo "Checking for new Deadlocked maps..."
    update_maps 'index_dl_ntsc.txt' 'dl' ''
    ;;

  5)
    echo "Checking for new UYA NTSC maps..."
    update_maps 'index_uya_ntsc.txt' 'uya' ''
    ;;

  6)
    echo "Checking for new R&C3 PAL maps..."
    update_maps 'index_uya_pal.txt' 'uya' '.pal'
    ;;

  *)
    echo "invalid option"
    exit
    ;;
esac

echo 
echo "Press any key to exit..."
read -n1
