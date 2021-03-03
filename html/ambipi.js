
var newImage = new Image();

function updateImage() {
    if (newImage.complete) {
           newImage.src = document.getElementById("img").src;
           var temp = newImage.src;
           document.getElementById("img").src = newImage.src;
           newImage = new Image();
           newImage.src = temp + "?" + new Date().getTime();
    }
    setTimeout(updateImage, 125);
};

function playpause()
{
 //  http://ataripi.fritz.box:8080/jsonrpc?Player.PlayPause
 // [{"jsonrpc":"2.0","method":"Player.PlayPause","params":[1,"toggle"],"id":43}]
}

function hexToRgb(hex) {
  var result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  return result ? {
    r: parseInt(result[1], 16),
    g: parseInt(result[2], 16),
    b: parseInt(result[3], 16)
  } : null;
}

$(function() {
  updateImage();
  
  $.get("/api/bri", function(data) { 
      $('#bri').val(parseInt(data));
  });

  $("#color").change(function(data) {
    var col = hexToRgb(data.target.value);
    $.get("/api/col/"+col.r+"/"+col.g+"/"+col.b, function(data) {
    });
  });  
  $("#ambilight").click(function(data) {
    $.get("/api/mode/ambilight", function(data) { });
  });
  $("#rainbow").click(function(data) {
    $.get("/api/mode/rainbow", function(data) { });
  });
  $("#testpattern").click(function(data) {
    $.get("/api/mode/testpattern", function(data) { });
  });
  $("#white").click(function(data) {
    $.get("/api/mode/white", function(data) { });
  });
  $("#off").click(function(data) {
    $.get("/api/mode/off", function(data) { });
  });  
  $("#bri0").click(function(data) {
    $.get("/api/bri/0", function(data) { });
    $('#bri').val(0);
  });  
  $("#bri25").click(function(data) {
    $.get("/api/bri/25", function(data) { });
    $('#bri').val(25);
  });  
  $("#bri50").click(function(data) {
    $.get("/api/bri/50", function(data) { });
    $('#bri').val(50);
  });  
  $("#bri100").click(function(data) {
    $.get("/api/bri/100", function(data) { });
    $('#bri').val(100);
  });  
  $("#bri").on("input change", function(data) {
    $.get("/api/bri/"+$('#bri').val(), function(data) { });
  });  
  $("#crop0").click(function(data) {
    $.get("/api/crop/0", function(data) { });
  });  
  $("#crop1").click(function(data) {
    $.get("/api/crop/1", function(data) { });
  });  
});
