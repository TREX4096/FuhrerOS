# FuhrerOS interactive shell setup
if [ -n "$PS1" ]; then
	if [ "$(id -u)" = 0 ]; then
		PS1='\[\e[1;31m\]fuhreros\[\e[0m\]:\w# '
	else
		PS1='\[\e[1;36m\]\u@fuhreros\[\e[0m\]:\w$ '
	fi
	alias ll='ls -la'
	alias fs='fuhrer status'
	if [ -r /run/fuhrer/status ]; then
		_p=$(sed -n 's/^policy=//p' /run/fuhrer/status)
		_m=$(sed -n 's/^mode=//p' /run/fuhrer/status)
		echo "fuhrerd: mode=$_m policy=$_p   (fuhrer status for details)"
		unset _p _m
	fi
fi
