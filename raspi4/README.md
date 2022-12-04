## license manager
license_server="http(s)://server_ip:port"

### request signature 

    curl -H "Content-type: application/octet-stream" -X POST --data-binary "@license/license.dat" ${license_server}/license/sign > sig.dat

### verify signature

    cat license/license.dat sig.dat > license_sig.dat
    curl -H "Content-type: application/octet-stream" -X POST --data-binary "@license_sig.dat" ${license_server}/license/verify



