
/* graph tricks.  stolen from: http://bl.ocks.org/d3noob/13a36f70a4f060b97e41
 */

function make_graph(data, uid, devname) {

  //console.log("WE ARE GRAPHIN: " + uid);

  // Set the dimensions of the canvas / graph
  var margin = {top: 30, right: 20, bottom: 30, left: 50},
    width = 600 - margin.left - margin.right,
      height = 270 - margin.top - margin.bottom;

    // Parse the date / time
    var parseDate = d3.time.format("%d-%b-%y").parse;

    // Set the ranges
    var x = d3.time.scale().range([0, width]);
    var y = d3.scale.linear().range([height, 0]);

    // Define the axes
    var xAxis = d3.svg.axis().scale(x)
      .orient("bottom").ticks(5);

    var yAxis = d3.svg.axis().scale(y)
      .orient("left").ticks(5);

    // Define the line
    var valueline = d3.svg.line()
      .x(function(d) { return x(d.time); })
      .y(function(d) { return y(d.val); });

    $(".graphdata").html("");

    if (devname.length > 0) {
      $("div.graphpop h2").html(devname);
    }
    $("div.graphpop h3").html(uid);

    $(".ingraph").each(function() {
     //$(this).attr("href", $(this).attr("href") + uid);
     this.href = $(this).attr("href").replace(/uid=.*/g, 'uid=' + uid);
     this.setAttribute('data-uid' ,uid);
     if (devname.length > 0) {
       this.setAttribute('data-name', devname);
     }
     //console.log($(this).attr("href"));
    });

    // Adds the svg canvas
    var svg = d3.select("body").select("div.graphdata")
      .append("svg")
      .attr("width", width + margin.left + margin.right)
      .attr("height", height + margin.top + margin.bottom)
      .append("g")
      .attr("transform", 
	    "translate(" + margin.left + "," + margin.top + ")");

    // Scale the range of the data
    x.domain(d3.extent(data, function(d) { return d.time; }));

    var min = d3.min(data, function(d) { return d.val; });
    if (min > 0.0) {
      min = min / 2.0;
    } /* Don't muck with negative bottom scales */
    y.domain([min, d3.max(data, function(d) { return d.val; })]);

    // Add the valueline path.
    svg.append("path")
        .attr("class", "line")
      .attr("d", valueline(data));

    // Add the X Axis
    svg.append("g")
        .attr("class", "x axis")
        .attr("transform", "translate(0," + height + ")")
        .call(xAxis);

    // Add the Y Axis
    svg.append("g")
        .attr("class", "y axis")
        .call(yAxis);

    var button = $("<a href='#graph_popup'></a>");
    button.appendTo('body');
    button[0].click();
    button.remove();
}

var subtypes = ["none",
		"switch", "outlet", "temp", "humid", "counter",
		"pressure", "speed", "dir", "moisture", "wetness",
		"hub", "lux", "voltage", "wattsec", "watt", "amps",
		"rainrate", "weather", "alarmstatus", "number", "percentage",
		"flowrate", "distance", "volume", "timer", "thmode",
		"thstate", "smnumber", "blind", "collector", "bool"];
var types = ["none",
	     "switch", "dimmer", "sensor", "timer", "blind"];
var prototypes = ["none",
		  "Insteon V1", "Insteon V2", "Insteon V2CS",
		  "OWFS", "Brultech GEM", "Brultech ECM 1240",
		  "WMR918", "AD2USB", "Icaddy", "Venstar", "URST-II",
		  "Collector"];


function fix_devedit_form(data) {
  $("div.editdevpop h2").html("Edit properties for " + data.name);
  $("div.editdevpop h3").html(data.uid);
  $("#edit-uid").val(data.uid);
  $("#edit-name").val(data.name);
  $("#edit-rrdname").val(data.rrdname);
  $("#edit-lowat").val(data.lowat);
  $("#edit-hiwat").val(data.hiwat);
  $("#edit-handler").val(data.handler);
  $("#edit-hargs").val(data.hargs);
  $("#edit-devt").html(types[data.devt]);
  $("#edit-subt").html(subtypes[data.subt]);
  $("#edit-proto").html(prototypes[data.proto]);

  $("#edit-availgroups").html("");
  $("#edit-groups").html("");

  $.each(data.groups, function(i, item) {
	   $("#edit-availgroups").append($('<option>', { value : item , text : item }));
	 });
  $.each(data.memberof, function(i, item) {
	   $("#edit-groups").append($('<option>', { value : item , text : item }));
	 });

}

/* ajax thingie for forms inspired by
   http://hayageek.com/jquery-ajax-form-submit/
*/
$(document).ready(function() {

  /* for the lights */

  $('.switchbutton').click(function() {
    //console.log("this should appear just once!");
    var postData = $(this.form).serializeArray();
    var formURL = $(this.form).attr("action");

    $.ajax({
      url: formURL,
      type: "POST",
      data: postData,
      dataType: "json",
      cache: false,
      });
  }); /* clickfunc */

  /* for the dimmers */

  $('.dimmerform').submit(function(e) {
    //console.log("DIMMER this should appear just once!");
    var postData = $(this).serializeArray();
    var formURL = $(this).attr("action");

    e.preventDefault();
    $.ajax({
      url: formURL,
      type: "POST",
      data: postData,
      dataType: "json",
      cache: false,
      });
  }); /* submitfunc */

  /* for the graphs */
  $('.graphbutton').click(function(e) {
    var formURL = $(this).attr("href");
    var uid = $(this).attr("data-uid");
    var name = $(this).attr("data-name");

    e.preventDefault();
    $.ajax({
      url: formURL,
      dataType: "json",
      cache: false,
      context: this,
      success: function(data){
	  make_graph(data.data, uid, name);
	},
	  error: function(ts) { 
	  console.log(ts.responseText); }
      });
  }); /* clickfunc */

  /* edit device form popup (populates the popup) */
  $('.devedit-link').click(function(e) {
  var formURL = $(this).attr("data-url");
  var uid = $(this).attr("data-uid");
  var port = $(this).attr("data-port");
  var host = $(this).attr("data-host");

  //e.preventDefault();
    $.ajax({
      url: formURL,
      dataType: "json",
      cache: false,
      data: {
	port : port,
	uid: uid,
	gnhastd: host
      },
      context: this,
      success: function(data){
	  fix_devedit_form(data);
	},
	  error: function(ts) { 
	  console.log(ts.responseText); }
      });
  }); /* clickfunc */

  /* code for the edit device window itself */

  $('.editdevform').submit(function(e) { return false; });

  $("#edit-chgdev, #edit-chggrp").click(function(e) {
    $('#edit-remove option').each(function(i) {
      $(this).attr("selected", "selected");  
    });
    var form = $(this).closest("form").get();
    var postData = $(form).serializeArray();
    postData.push({name: $(this).attr("name"), value: $(this).attr("value")});
    var formURL = $(form).attr("action");
    var inputval = $(this).attr("value");

    e.preventDefault();
    $.ajax({
      url: formURL,
      type: "POST",
      data: postData,
      dataType: "json",
      cache: false,
    });

    if (inputval === "Modify Device") {
      $("#edit-dev-post").html("Update complete");
    }
    if (inputval === "Modify Group Membership") {
      $("#edit-grp-post").html("Update complete");
    }
  }); /* submitfunc */

  /* fiddle the add/remove buttons on the edit dev form */
  $('#edit-add').click(function() {  
    return !$('#edit-availgroups option:selected').remove().appendTo('#edit-groups');
  });  
  $('#edit-remove').click(function() {  
    return !$('#edit-groups option:selected').remove().appendTo('#edit-availgroups');
  });

}); /*docready */

function getCookie(cname) {
    var name = cname + "=";
    var ca = document.cookie.split(';');
    for(var i=0; i<ca.length; i++) {
        var c = ca[i];
        while (c.charAt(0)==' ') c = c.substring(1);
        if (c.indexOf(name) == 0) return c.substring(name.length,c.length);
    }
    return "";
}

/* https://github.com/domharrington/js-number-abbreviate */


function N(num, places) {
  return +(Math.round(num + "e+" + places)  + "e-" + places);
}

function numAbbr(num, decPlaces) {
  var abbrev = ['k', 'm', 'b', 't'];
  var number = N(num, decPlaces);

  decPlaces = Math.pow(10, decPlaces);
  for (var i = abbrev.length - 1; i >= 0; i--) {
    var size = Math.pow(10, (i + 1) * 3);
      if (size <= number) {
	number = Math.round(number * decPlaces / size) / decPlaces;
	if ((number === 1000) && (i < abbrev.length - 1)) {
	  number = 1;
	  i++;
	}
	number += abbrev[i];
	break;
      }
  }
  return number;
}

function fixnumber(val) {
  var num = Number(val);

  if (getCookie("abbreviate") == "" || getCookie("abbreviate") == "0") {
    return val.replace(/(\.[0-9][0-9])[0-9]*$/,'$1').replace(/\.00/, '');
  }

  if (isNaN(num)) {
    return val;
  }

  if (val > 10000) {
    return(numAbbr(num, 4));
  }

  return(numAbbr(num, 2));
}

/* horrible code for sse, cobbled together via guesses */

if (typeof(EventSource)=="undefined") {
  alert("Sorry!  This particular page doesn't work well with your browser.");
}

var ssesource = new EventSource("/cgi-bin/jsoncoll.cgi");
ssesource.onmessage = function(event) {
  var gdata = JSON.parse(event.data);

  for(var i = 0; i < gdata.length; i++) {
    var obj = gdata[i];

    //console.log("got line, " + obj.uid + " id:" + i);

    //if (document.getElementById(obj.uid) != null) {
      var z = document.getElementsByName(obj.uid + "-val");
      //console.log("length of z for " + obj.uid + " is " + z);
      if (obj.subt == "1" && (obj.type == "1" || obj.type =="3")) {
	/* switch/switch */
	var x = document.getElementsByName(obj.uid);

	for (j=0; j < x.length; j++) {
	  if (obj.value == "0") {
	    for (k=0; k < z.length; k++) {
	      z[k].innerHTML = "OFF";
	    }
	    if (obj.type == "1") { x[j].checked = false; }
	  } else {
	    for (k=0; k < z.length; k++) {
	      z[k].innerHTML = "ON";
	    }
	    if (obj.type == "1") { x[j].checked = true; }
	  }
	}
      } else if (obj.subt == "1" && obj.type == "2") { /* dimmer/switch */
	var x = document.getElementsByName(obj.uid);
	for (j=0; j < x.length; j++) {
	  var nval = obj.value * 100.0;
	  x[j].value = ~~nval;
	  for (k=0; k < z.length; k++) {
	    z[k].innerHTML = ~~nval + "%";
	  }
	}
      } else {
	  for (k=0; k < z.length; k++) {
	    z[k].innerHTML = fixnumber(obj.value);
	    if (obj.subt == "3" || obj.subt == "8") { /* temp, dir */
	      z[k].innerHTML += "<i class=\"wi wi-degrees\"></i>";
	    } else if (obj.subt == "4" || obj.subt == "10") {
	      z[k].innerHTML += "%";
	    }
	  }
      }
      //}
  }
}


/* weather code for forecast widget.  Uses simpleweather:
http://simpleweatherjs.com/
*/
function setWeatherIcon(condid) {
switch(condid) {
case '0': var icon = '<i class="wi wi-tornado"></i>';
break;
case '1': var icon = '<i class="wi wi-storm-showers"></i>';
break;
case '2': var icon = '<i class="wi wi-tornado"></i>';
break;
case '3': var icon = '<i class="wi wi-thunderstorm"></i>';
break;
case '4': var icon = '<i class="wi wi-thunderstorm"></i>';
break;
case '5': var icon = '<i class="wi wi-snow"></i>';
break;
case '6': var icon = '<i class="wi wi-rain-mix"></i>';
break;
case '7': var icon = '<i class="wi wi-rain-mix"></i>';
break;
case '8': var icon = '<i class="wi wi-sprinkle"></i>';
break;
case '9': var icon = '<i class="wi wi-sprinkle"></i>';
break;
case '10': var icon = '<i class="wi wi-hail"></i>';
break;
case '11': var icon = '<i class="wi wi-showers"></i>';
break;
case '12': var icon = '<i class="wi wi-showers"></i>';
break;
case '13': var icon = '<i class="wi wi-snow"></i>';
break;
case '14': var icon = '<i class="wi wi-storm-showers"></i>';
break;
case '15': var icon = '<i class="wi wi-snow"></i>';
break;
case '16': var icon = '<i class="wi wi-snow"></i>';
break;
case '17': var icon = '<i class="wi wi-hail"></i>';
break;
case '18': var icon = '<i class="wi wi-hail"></i>';
break;
case '19': var icon = '<i class="wi wi-cloudy-gusts"></i>';
break;
case '20': var icon = '<i class="wi wi-fog"></i>';
break;
case '21': var icon = '<i class="wi wi-fog"></i>';
break;
case '22': var icon = '<i class="wi wi-fog"></i>';
break;
case '23': var icon = '<i class="wi wi-cloudy-gusts"></i>';
break;
case '24': var icon = '<i class="wi wi-cloudy-windy"></i>';
break;
case '25': var icon = '<i class="wi wi-thermometer"></i>';
break;
case '26': var icon = '<i class="wi wi-cloudy"></i>';
break;
case '27': var icon = '<i class="wi wi-night-cloudy"></i>';
break;
case '28': var icon = '<i class="wi wi-day-cloudy"></i>';
break;
case '29': var icon = '<i class="wi wi-night-cloudy"></i>';
break;
case '30': var icon = '<i class="wi wi-day-cloudy"></i>';
break;
case '31': var icon = '<i class="wi wi-night-clear"></i>';
break;
case '32': var icon = '<i class="wi wi-day-sunny"></i>';
break;
case '33': var icon = '<i class="wi wi-night-clear"></i>';
break;
case '34': var icon = '<i class="wi wi-day-sunny-overcast"></i>';
break;
case '35': var icon = '<i class="wi wi-hail"></i>';
break;
case '36': var icon = '<i class="wi wi-day-sunny"></i>';
break;
case '37': var icon = '<i class="wi wi-thunderstorm"></i>';
break;
case '38': var icon = '<i class="wi wi-thunderstorm"></i>';
break;
case '39': var icon = '<i class="wi wi-thunderstorm"></i>';
break;
case '40': var icon = '<i class="wi wi-storm-showers"></i>';
break;
case '41': var icon = '<i class="wi wi-snow"></i>';
break;
case '42': var icon = '<i class="wi wi-snow"></i>';
break;
case '43': var icon = '<i class="wi wi-snow"></i>';
break;
case '44': var icon = '<i class="wi wi-cloudy"></i>';
break;
case '45': var icon = '<i class="wi wi-lightning"></i>';
break;
case '46': var icon = '<i class="wi wi-snow"></i>';
break;
case '47': var icon = '<i class="wi wi-thunderstorm"></i>';
break;
case '3200': var icon = '<i class="wi wi-cloud"></i>';
break;
default: var icon = '<i class="wi wi-cloud"></i>';
break;
}
return icon;
}

$(document).ready(function() {  
  getWeather(); //Get the initial weather.
  setInterval(getWeather, 600000); //Update the weather every 10 minutes.
});

function getWeather() {
  $.simpleWeather({
    location: localzip,
    unit: 'f',
    success: function(weather) {
	html = '<div class="widgetcontainer"><div class="widgetcard">';
	html += '<div class="header">Forecast for '+localzip+'</div>';
	html += '<div class="container"><div class="row">';
	html += '<div class="fcbox">'+setWeatherIcon(weather.code)+'</div>';
	html += '<div class="bigtext">'+weather.temp+'</div>';
	html += '<div class="smalltext">';
	html += '<div class="sidetext"><div class="title">High</div><div>'+weather.high+'</div></div>';
	html += '<div class="sidetext"><div class="title">Low</div><div>'+weather.low+'</div></div>';
	html += '</div>';
	html += '</div>'; /*row end*/
	for (day=0; day < 5; day++) {
	  html += '<div class="row">';
	  html += '<div class="smalltext">'+weather.forecast[day].day+'</div>';
	  html += '<div class="smalltext">'+setWeatherIcon(weather.forecast[day].code)+'</div>';
	  html += '<div class="smalltext">High:'+weather.forecast[day].high+'</div>';
	  html += '<div class="smalltext">Low:'+weather.forecast[day].low+'</div>';
	  
	  html += '</div>'; /*row end*/
	}

	html += '</div></div></div>';
  
      $(".weatherwidget").html(html);
    },
    error: function(error) {
      $(".weatherwidget").html('<p>'+error+'</p>');
    }
  });
}



  $(function()
    {
      $('.scroll-pane').jScrollPane();
    });

/* auto-resize the scroller */
$(function() {
    $('.scroll-pane').each(function() {
     $(this).jScrollPane({showArrows: $(this).is('.arrow')});
     var api = $(this).data('jsp');
     var throttleTimeout;
     $(window).bind('resize',function(){
      if (!throttleTimeout) {
	throttleTimeout = setTimeout(function() {
	       api.reinitialise();
	       throttleTimeout = null;
        }, 50);
      }
     });
   })
});

function goFullscreen() {
  if (document.body.mozRequestFullScreen) {
    // This is how to go into fullscren mode in Firefox
    // Note the "moz" prefix, which is short for Mozilla.
    document.body.mozRequestFullScreen();
  } else if (document.body.webkitRequestFullScreen) {
    // This is how to go into fullscreen mode in Chrome and Safari
    // Both of those browsers are based on the Webkit project,
    // hence the same prefix.
    document.body.webkitRequestFullScreen();
  }
  // Hooray, now we're in fullscreen mode!
}

/* Slider JS */
var range_el = document.querySelector('[class*=webkit] input[type=range]'), 
  style_el, sel, pref, comps, a, b;

if(range_el) {
  style_el = document.createElement('style');
  sel = '.js[class*="webkit"] input[type=range]';
  pref = '-webkit-slider-';
  comps = ['runnable-track', 'thumb'];
  a = ':after'; b = ':before';
      
  document.body.appendChild(style_el);
      
  range_el.addEventListener('input', function() {
	var str = '', 
	curr_val = this.value, 
	min = this.min || 0, 
	max = this.max || 100, 
	perc = 100*(curr_val - min)/(max - min), 
	fill_val = ((perc <= 5)?'30px':((~~perc) + '%')) + ' 100%', 
	s_total = 60*curr_val, 
	ss = ~~(s_total%60), 
	m = Math.floor(s_total/60), speaker_rules;

	if(ss < 10) { ss = '0' + ss; }
	str += sel + '::' + pref + comps[0] + '{background-size:'
	  + fill_val + '}';

	str += sel + '::' + pref + comps[1] + a + ',' + 
	  sel + ' /deep/ #' + comps[1] + a + '{content:"' + m + ':' + ss + '"}';
	speaker_rules = 'opacity:' + Math.min(1, perc/50).toFixed(2) + ';' + 
	  'color:rgba(38,38,38,' + 
	  ((perc <= 50) ? 0 : (((perc - 50)/50).toFixed(2))) + ')';
    
	str += sel + '::' + pref + comps[0] + b + ',' + 
	  sel + ' /deep/ #' + comps[0] + b + '{' + speaker_rules + '}';
        
	style_el.textContent = str;
  }, false);
}
