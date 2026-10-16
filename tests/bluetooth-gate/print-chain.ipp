# Print-Job with the chain-only template: ipptool -d copies=2 -f label.png URI print-chain.ipp
{
  NAME "Print-Job ptouch-chain copies=$copies"
  OPERATION Print-Job
  GROUP operation-attributes-tag
  ATTR charset attributes-charset utf-8
  ATTR naturalLanguage attributes-natural-language en
  ATTR uri printer-uri $uri
  ATTR name requesting-user-name bluetooth-gate
  ATTR mimeMediaType document-format image/png
  GROUP job-attributes-tag
  ATTR integer copies $copies
  ATTR collection finishings-col {
    MEMBER keyword finishing-template ptouch-chain
  }
  FILE $filename
  STATUS successful-ok
  EXPECT job-id
}
