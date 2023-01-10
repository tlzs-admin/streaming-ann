
## fifo pipe as stdin
tmpd="$(mktemp -d)"
tmpf="$tmpd"/fifo
mkfifo "$tmpf"
printf "%s\n" "$tmpf" 

## open the fifo and don't close it
exec 3> "$tmpf"

## test websocket 

	$ socat - TCP4:localhost:9001

```

GET /ws-source HTTP/1.1
Connection: Upgrade
Upgrade: websocket
Host: localhost:9001
Sec-WebSocket-Version: 13
Sec-WebSocket-Key: MDEyMzQ1Njc4OWFiY2RlZg==

```



## test redirection
### redirection.sh

    #!/bin/bash
    
    echo 'exec redirection begin'
    
    exec 3>&1
    exec >> file.txt
    
    echo 'aaa'
    echo 'bbb'
    echo 'ccc'
    
    exec >&3
    exec 3>&-
    
    echo 'exec redirection end'


