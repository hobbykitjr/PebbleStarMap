// Pixel Stars — send phone GPS location to watch

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  getLocation();
});

function getLocation() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      // Send lat/lon as integers (x100 for 2 decimal precision)
      var lat = Math.round(pos.coords.latitude * 100);
      var lon = Math.round(pos.coords.longitude * 100);
      console.log('Location: ' + lat/100 + ', ' + lon/100);
      Pebble.sendAppMessage({'LAT': lat, 'LON': lon},
        function() { console.log('Location sent'); },
        function() { console.log('Location send failed'); }
      );
    },
    function(err) {
      console.log('Location error: ' + err.message);
    },
    {timeout: 15000, maximumAge: 300000}
  );
}

Pebble.addEventListener('appmessage', function(e) {
  getLocation();
});
