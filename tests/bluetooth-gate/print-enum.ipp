# Print-Job with an enum finishing: ipptool -d finishings=11 -d copies=1 -f label.png URI print-enum.ipp
{
  NAME "Print-Job finishings=$finishings copies=$copies"
  OPERATION Print-Job
  GROUP operation-attributes-tag
  ATTR charset attributes-charset utf-8
  ATTR naturalLanguage attributes-natural-language en
  ATTR uri printer-uri $uri
  ATTR name requesting-user-name bluetooth-gate
  ATTR mimeMediaType document-format image/png
  GROUP job-attributes-tag
  ATTR integer copies $copies
  ATTR enum finishings $finishings
  FILE $filename
  STATUS successful-ok
  EXPECT job-id
}
