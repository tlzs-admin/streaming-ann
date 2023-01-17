const base_url = window.location.origin;
const img = document.getElementById("frames");
const container1 = document.getElementById("container1");


const fps = 5;
var channel = null;
var selected_item = null;

const alert_file_url = "audio/alert.mp3";
var alert_flags = 0;
function play_alert_sound()
{
	console.log(Date.now() + ": " + alert_file_url + ': flags=' + alert_flags);
	if(!alert_flags) {
		let audio = new Audio(alert_file_url);
		audio.onplay = () => { alert_flags = 1; };
		audio.onended = () => { alert_flags = 0; } ;
		audio.play();
	}
}

var toolbar1 = {
	view: "toolbar", padding: 3,
	
	css: "webix_dark",
	elements: [
		{ view: "icon", icon: "mdi mdi-menu", click: function() { 
				$$("$sidebar1").toggle(); 
				resize_frame();
			}
		},
		{ view: "label", id: "app_title", label: document.title },
		{},
		{ view: "button", id: "test_alert", click: play_alert_sound, value: "alert" },
		{ view: "icon", id: "bell", badge: 0, icon: "mdi mdi-bell" }
	]
	
};

const sidebar1 = {
	view: "sidebar",
	name: "sidebar1",
	css: "webix_dark",
	multipleOpen: true,
	url: "query/sidebar1.json",
	on: {
		onAfterSelect: on_sidebar1_selected_changed
	}
}

const viewer = {
	view: "scrollview",
	scroll: "y",
	body: {
		content: "container1"
	}
}

function poll_frame()
{
	console.log('poll_frame(' + channel + ')');
	let new_frame = new Image();
	console.log(selected_item);
	
	new_frame.onload = () => {
		image_valid = true;
		img.src = new_frame.src;
		
		if(!selected_item.errcode) selected_item.errcode = 0;
		
		if(selected_item.errcode) {
			selected_item.errcode = 0;
			selected_item.icon = "mdi mdi-webcam";
			$$("$sidebar1").refresh();
		}
		
		setTimeout(poll_frame, 1000.0 / fps);
	}
	new_frame.onerror = () => {
		if(selected_item.errcode !== 1) {
			selected_item.errcode = 1;
			selected_item.icon = "mdi mdi-webcam-off";
			$$("$sidebar1").refresh();
		}
		
		// retry loading every 10 seconds
		img.src = "data:image/gif;base64,R0lGODlhAQABAAD/ACwAAAAAAQABAAACADs=";
		setTimeout(poll_frame, 10000); 
	}
	
	new_frame.src = base_url + '/' + channel;
}

function on_sidebar1_selected_changed(item_id)
{
	console.log(item_id);
	const keys = item_id.split("/");
	let region = keys[0];
	let channel_name = keys[1];
	
	selected_item = $$("$sidebar1").getSelectedItem(item_id)[0];
	
	console.log('  region: ' + region + ', channel: ' + channel_name);
	
	channel = item_id;
	poll_frame();
	resize_frame();
}

window.onload = (event) => {
	
}

function resize_frame()
{
	const ratio = 9.0 / 16.0;
	let max_width = container1.clientWidth;
	let max_height = container1.clientHeight;
	
	let new_width = img.width;
	let new_height = img.height;
	
	
	new_width = max_width * 0.95;
	new_height = new_width * ratio;
	
	if(new_height > max_height) {
		new_height = max_height * 0.95;
		new_width = new_height / ratio;
	}
	
	
	img.width = new_width;
	img.height = new_height;
	
	console.log(img.width + " x " + img.height);
}

window.onresize = (event) => {
	resize_frame();
}

webix.ready(function(){
	
	
	webix.ui({
		margin: 5, 
		type: "space",
		
		rows: [
			toolbar1,
			{
				cols: [ 
					sidebar1,
					viewer
				]
			
			}
		]
	});
});
