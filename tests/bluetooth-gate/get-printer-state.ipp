# Loaded tape and printer state: ipptool -tv URI get-printer-state.ipp
{
  NAME "Get-Printer-Attributes state and media-ready"
  OPERATION Get-Printer-Attributes
  GROUP operation-attributes-tag
  ATTR charset attributes-charset utf-8
  ATTR naturalLanguage attributes-natural-language en
  ATTR uri printer-uri $uri
  ATTR name requesting-user-name bluetooth-gate
  ATTR keyword requested-attributes printer-state,printer-state-reasons,media-ready
  STATUS successful-ok
}
