// Pixel Stars — location + ISS tracking

var userLat = 40.5, userLon = -76.0;  // Defaults (PA)
var issTimer = null;

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  getLocation();
});

function getLocation() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      userLat = pos.coords.latitude;
      userLon = pos.coords.longitude;
      var lat = Math.round(userLat * 100);
      var lon = Math.round(userLon * 100);
      console.log('Location: ' + userLat + ', ' + userLon);
      Pebble.sendAppMessage({'LAT': lat, 'LON': lon},
        function() {
          console.log('Location sent');
          fetchISS();  // Start ISS tracking after location
          // Poll ISS every 30 seconds
          if(issTimer) clearInterval(issTimer);
          issTimer = setInterval(fetchISS, 30000);
        },
        function() { console.log('Location send failed'); }
      );
    },
    function(err) {
      console.log('Location error: ' + err.message);
      // Still try ISS with defaults
      fetchISS();
      if(issTimer) clearInterval(issTimer);
      issTimer = setInterval(fetchISS, 30000);
    },
    {timeout: 15000, maximumAge: 300000}
  );
}

function fetchISS() {
  // Open Notify API — gives ISS lat/lon position
  var url = 'http://api.open-notify.org/iss-now.json';
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    if(xhr.status === 200) {
      try {
        var j = JSON.parse(xhr.responseText);
        var issLat = parseFloat(j.iss_position.latitude);
        var issLon = parseFloat(j.iss_position.longitude);
        console.log('ISS at: ' + issLat + ', ' + issLon);

        // Convert ISS lat/lon to alt/az from observer
        var altaz = issAltAz(issLat, issLon, userLat, userLon);
        console.log('ISS alt: ' + altaz.alt + ', az: ' + altaz.az);

        // Send to watch: alt/az as integers (x10 for precision)
        var visible = altaz.alt > 0 ? 1 : 0;
        Pebble.sendAppMessage({
          'ISS_ALT': Math.round(altaz.alt * 10),
          'ISS_AZ': Math.round(altaz.az * 10),
          'ISS_VIS': visible
        },
        function() { console.log('ISS data sent'); },
        function() { console.log('ISS send failed'); });
      } catch(e) {
        console.log('ISS parse error: ' + e);
      }
    }
  };
  xhr.onerror = function() { console.log('ISS fetch error'); };
  xhr.open('GET', url);
  xhr.send();
}

// Convert ISS ground position to observer alt/az
// ISS orbits at ~420km altitude
function issAltAz(issLat, issLon, obsLat, obsLon) {
  var R = 6371;  // Earth radius km
  var H = 420;   // ISS altitude km
  var deg2rad = Math.PI / 180;

  var dLat = (issLat - obsLat) * deg2rad;
  var dLon = (issLon - obsLon) * deg2rad;
  var lat1 = obsLat * deg2rad;
  var lat2 = issLat * deg2rad;

  // Haversine for angular distance
  var a = Math.sin(dLat/2)*Math.sin(dLat/2) +
          Math.cos(lat1)*Math.cos(lat2)*Math.sin(dLon/2)*Math.sin(dLon/2);
  var c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
  var groundDist = c;  // Angular distance in radians

  // Altitude angle: atan((cos(c) - R/(R+H)) / sin(c))
  var cosC = Math.cos(groundDist);
  var sinC = Math.sin(groundDist);
  var ratio = R / (R + H);
  var alt = Math.atan2(cosC - ratio, sinC) / deg2rad;

  // Azimuth: bearing from observer to ISS ground point
  var y = Math.sin(dLon) * Math.cos(lat2);
  var x = Math.cos(lat1)*Math.sin(lat2) - Math.sin(lat1)*Math.cos(lat2)*Math.cos(dLon);
  var az = Math.atan2(y, x) / deg2rad;
  if(az < 0) az += 360;

  return {alt: alt, az: az};
}

Pebble.addEventListener('appmessage', function(e) {
  getLocation();
});
