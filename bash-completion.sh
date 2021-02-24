_pspg_completion() {
       local w0 w1 w2 w3

       COMPREPLY=()
       w0=${COMP_WORDS[COMP_CWORD]}
       w1=${COMP_WORDS[COMP_CWORD-1]}
       w2=${COMP_WORDS[COMP_CWORD-2]}
       w3=${COMP_WORDS[COMP_CWORD-3]}

       BOOLEAN_VALUES=(
               "on"
               "off"
       )

         if [[ "$w1" == "--csv-header"                                     ]];    then COMPREPLY=($(compgen -W "${BOOLEAN_VALUES[*]}" -- "$w0"))

       else
               OPTIONS=(
                       "--about"
                       "--ascii"
                       "--blackwhite"
                       "--help"
                       "--version"
                       "--file="
                       "--freezecols"
                       "--quit-if-one-screen"
                       "--hold-stream="
                       "--interactive"
                       "--ignore_file_suffix"
                       "--ni"
                       "--no-watch-file"
                       "--no-mouse"
                       "--no-sigint-search-reset"
                       "--only-for-tables"
                       "--no-sigint-exit"
                       "--pgcli-fix"
                       "--quit-on-f3"
                       "--reprint-on-exit"
                       "--rr="
                       "--stream"
                       "--style"
                       "--bold-labels"
                       "--bold-cursor"
                       "--border"
                       "--double-header"
                       "--force-uniborder"
                       "--ignore-bad-rows"
                       "--null="
                       "--hlite-search"
                       "--ignore-case"
                       "--IGNORE-CASE"
                       "--less-status-bar"
                       "--line-numbers"
                       "--no-bars"
                       "--no-commandbar"
                       "--no-topbar"
                       "--no-cursor"
                       "--no-sound"
                       "--tabular-cursor"
                       "--vertical-cursor"
                       "--clipboard_app"
                       "--csv"
                       "--csv-separator"
                       "--csv-header"
                       "--skip-columns-like="
                       "--tsv"
                       "--query="
                       "--watch"
                       "--dbname="
                       "--host="
                       "--port="
                       "--username="
                       "--password"
                       "--log="
                       "--wait="

               )
               COMPREPLY=($(compgen -W "${OPTIONS[*]}" -- "$w0"))
       fi
}

complete -F _pspg_completion pspg
