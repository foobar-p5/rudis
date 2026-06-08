_rudis() {
    local cur=${COMP_WORDS[COMP_CWORD]}
    COMPREPLY=($(compgen -W "list cue play pause toggle next previous status" -- "$cur"))
}
complete -F _rudis rudis
