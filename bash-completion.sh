_pspg()
{
    local cur prev words cword
    _init_completion || return

    case $prev in
        -f | --file | --log)
           _filedir
           return
           ;;
        --csv-header)
           COMPREPLY=($(compgen -W 'on off' -- "$cur"))
           return
           ;;
        --hold-stream)
           COMPREPLY=($(compgen -W '1 2 3' -- "$cur"))
           return
           ;;
        --border)
           COMPREPLY=($(compgen -W '0 1 2' -- "$cur"))
           return
           ;;
    esac

    if [[ $cur == -* ]]; then
       COMPREPLY=($(compgen -W '$(_parse_help "$1" --help)' -- "$cur"))
       return
    fi

    _filedir

} &&
    complete -F _pspg pspg
