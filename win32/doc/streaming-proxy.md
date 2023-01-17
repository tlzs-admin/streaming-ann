## Streaming Proxy

### I. Settings template

    "stream_proxy": {
    	"port": 8800,
    	"endpoint_base": "/default",
    	"channels": [
    		"channel0", "channel1", "channel2", "channel3",
    	],
    	"viewer_path": "/viewer",
    	"viewer_html": "video-player5.html",
    	"config_path": "/config",
    }
    
    
### II. HTTP methods and responses
##### HTTP GET
- PATH: <base_url>/config/list-all
  + query_string: (null) 
  + response:
      [
        { "channel_name": ..., ... },
        { "channel_name": ..., ... },
      ]
 
- PATH: <base_url>/config/channel(n)
  + query_string: ///< @todo  
  + response: 
      {
        "channel_name": "<channel_name>",
        "detection_mode": <mode>
      }


##### HTTP POST
- PATH: <base_url>/config/channel(n)
  + query_string: "?channel_name=<channel_name>&detection_mode=<mode>"
  + post_fields: { "channel_name": "<channel_name>", "detection_mode": <mode> }
  + response: 
      {
          "err_code": 0,
          "channel_name": "<channel_name>",
          "field": "detection_mode",
          "current_value": %d,
          "status": "<status>", // "OK" | "NG",
          "description": "err message"
      }
      


#### Usuage: 
 - modify config:
    
    curl -X POST http://localhost:8800/config/channel0?detection_mode=1

 - list-all:
    
    curl http://localhost:8800/config/list-all
    
 - query settings:
    
    curl http://localhost:8800/config/channel0
    
    
