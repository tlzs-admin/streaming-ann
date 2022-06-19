const settings = document.getElementById('settings');
const controls = document.querySelector('.controls');
const camera_options = document.querySelector('.video-options>select');

const video = document.getElementById('camera1');
const canvas = document.getElementById('canvas1');
const buttons = [...controls.querySelectorAll('button')];
const [play, pause, screenshot] = buttons;

const facing_default_btn = document.getElementById('facing-default');
const facing_front_btn = document.getElementById('facing-front');
const facing_rear_btn = document.getElementById('facing-rear');
let facing_mode = 'user';
let stream_started = false;
let start_time = 0.0;
let frame_number = 0;


const video_constraints = {
	width: {
		min: 1280,
		ideal: 1920,
		max: 2560,
	},
	height: {
		min: 720,
		ideal: 1080,
		max: 1440
	},
};

facing_default_btn.onclick = () => {
	facing_mode = '';
	facing_default_btn.classList.remove('btn-secondary');
	facing_default_btn.classList.add('btn-primary');
	
	facing_front_btn.classList.remove('btn-primary');
	facing_rear_btn.classList.remove('btn-primary');
	
	facing_front_btn.classList.add('btn-secondary');
	facing_rear_btn.classList.add('btn-secondary');
	
	const updatedConstraints = {
		video: {
			...video_constraints,
		},
		deviceId: {
			exact: camera_options.value
		}
	};
	
	console.log(updatedConstraints);
	
	start_stream(updatedConstraints);
}


facing_front_btn.onclick = () => {
	facing_mode = 'user';
	
	facing_front_btn.classList.remove('btn-secondary');
	facing_front_btn.classList.add('btn-primary');
	
	facing_default_btn.classList.remove('btn-primary');
	facing_rear_btn.classList.remove('btn-primary');
	
	facing_default_btn.classList.add('btn-secondary');
	facing_rear_btn.classList.add('btn-secondary');
	
	const updatedConstraints = {
		video: {
			...video_constraints,
			facingMode: { exact: facing_mode}
		},
		deviceId: {
			exact: camera_options.value
		}
	};
	
	console.log(updatedConstraints);
	
	start_stream(updatedConstraints);
}


facing_rear_btn.onclick = () => {
	facing_mode = 'user';
	
	facing_rear_btn.classList.remove('btn-secondary');
	facing_rear_btn.classList.add('btn-primary');
	
	facing_default_btn.classList.remove('btn-primary');
	facing_front_btn.classList.remove('btn-primary');
	
	facing_default_btn.classList.add('btn-secondary');
	facing_front_btn.classList.add('btn-secondary');
	
	const updatedConstraints = {
		video: {
			...video_constraints,
			facingMode: { exact: facing_mode}
		},
		deviceId: {
			exact: camera_options.value
		}
	};
	
	console.log(updatedConstraints);
	
	start_stream(updatedConstraints);
}


const constraints = [
	video_constraints,
	{
		...video_constraints,
		facingMode: { exact: 'user' }
	},
	{
		...video_constraints,
		facingMode: { exact: 'environment' }
	}
];

const query_camera_options = async () => {
  const devices = await navigator.mediaDevices.enumerateDevices();
  const videoDevices = devices.filter(device => device.kind === 'videoinput');
  const options = videoDevices.map(videoDevice => {
    return `<option value="${videoDevice.deviceId}">${videoDevice.label}</option>`;
  });
  camera_options.innerHTML = options.join('');
};



camera_options.onchange = () => {
	const updatedConstraints = {
		...constraints[1],
		deviceId: {
			exact: cameraOptions.value
		}
	};
	start_stream(updatedConstraints);
};

const start_stream = async (constraints) => {
	const stream = await navigator.mediaDevices.getUserMedia(constraints);
	video.srcObject = stream;
	play.classList.add('d-none');
	pause.classList.remove('d-none');
	screenshot.classList.remove('d-none');
	
	video.addEventListener('play', () => {
	if (!('requestVideoFrameCallback' in HTMLVideoElement.prototype)) {
		return alert('Your browser does not support the `Video.requestVideoFrameCallback()` API.');
		}
	});
	
	stream_started = true;
	video.requestVideoFrameCallback(update_frame);
}


play.onclick = () => {
	if(stream_started) {
		video.play();
		play.classList.add('d-none');
		pause.classList.remove('d-none');
		return;
	}
	
	if ('mediaDevices' in navigator && navigator.mediaDevices.getUserMedia) 
	{
		const updatedConstraints = {
		  ...constraints,
		  deviceId: {
			exact: camera_options.value
		  }
		};
		start_stream(updatedConstraints);
	}
};

pause.addEventListener('click', () => { 
	video.pause();
	play.classList.remove('d-none');
	pause.classList.add('d-none');
});

const fpsInfo = document.querySelector("#fps-info");
const metadataInfo =  document.querySelector("#metadata-info");
const surface = document.createElement("canvas");

video.addEventListener('loadedmetadata', () => {
	canvas.width = video.videoWidth;
	canvas.height = video.videoHeight;
	
	surface.style = "display: none";
	surface.width = video.videoWidth;
	surface.height = video.videoHeight;
});



/*
 * HTTP POST:
 * 	 url: AI-server URL
 *   blob: raw image_data (image/jpeg or image/png)
 *   Request:
 * 		Method: POST
 * 		Content-Type: blob.type
 *   Response:
 * 		Json result.
 * 	{ "detections": [
 *		{ "class": <class_index>, 	// (int)
 * 		// (double) relative coordination, range: [ 0.0 ,1.0 ]
 * 		  "x": x,					
 * 		  "y": y,
 * 		  "width": width,
 * 		  "height": height,
 * 		  "confidence": 0.99,
 * 		  //... custom data
 *      },
 * 		// ...
 * 	 ]
 * }
 */
const draw_frame = (response) => {
	let width = canvas.width;
	let height = canvas.height;
	
	let error_code = response['error_code'];
	let detections = response['detections'];
	
	console.log("image size: " + width + "x" + height);
	console.log("error_code: " + response['error_code']);
	
	const cr = canvas.getContext("2d");
	cr.drawImage(surface, 0, 0, width, height);
	
	cr.font = "20px Arial";
	cr.strokeStyle = "#ffff00";
	cr.lineWidth = 2;
	
	if(detections) {
		cr.beginPath();
		for(let i = 0; i < detections.length; ++i) {
			let bbox = detections[i];
			let x = bbox['left'] * width;
			let y = bbox['top'] * height;
			let cx = bbox['width'] * width;
			let cy = bbox['height'] * height;
			
			cr.rect(10, 10, width, height);
			
			cr.fillStyle = "blue";
			cr.fillText(det['class'], x, y);
		}
		cr.stroke();
	}
	
	video.requestVideoFrameCallback(update_frame);
}

const make_request = (method, url, blob = {}) => {
	const xhr = new XMLHttpRequest();
	xhr.open(method, url, true);
	if(blob) {
		xhr.setRequestHeader('Content-Type', blob.type);	// image/jpeg
	}
	xhr.onload = () => {
		console.log("onload::status: " + xhr.status + ", response: " + xhr.responseText);
		if(xhr.status >= 200 && xhr.status < 300) {
			let response = JSON.parse(xhr.responseText);
			if(response) {
				draw_frame(response);
			}
		}
	};
	
	xhr.onerror = () => {
		console.log("onerror::status: " + xhr.status + ", response: " + xhr.responseText);
		draw_frame();
	};
	
	xhr.send(blob);
}


const ai_process = (blob) => {
	const url = window.location;
	make_request("POST", url, blob);
}

const update_frame = (now, metadata) => {
	if (start_time === 0.0) {
	  start_time = now;
	}
	
	let width = canvas.width;
	let height = canvas.height;
	
	surface.style = "display: none";
	surface.width = width;
	surface.height = height;
	const ctx = surface.getContext("2d");
	ctx.drawImage(video, 0, 0, width, height);
	
	surface.toBlob(ai_process, "image/jpeg");
	
	
	
	const elapsed = (now - start_time) / 1000.0;
	const fps = (++frame_number / elapsed).toFixed(3);
	fpsInfo.innerText = !isFinite(fps) ? 0 : fps;
	metadataInfo.innerText = JSON.stringify(metadata, null, 2);
	

	console.log(metadata);
	
};  

window.onload = () => {
	query_camera_options();
}
