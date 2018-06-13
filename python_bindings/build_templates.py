  import jinja2

  templateLoader = jinja2.FileSystemLoader(searchpath="./files")
  templateEnv = jinja2.Environment(loader=templateLoader)
  TEMPLATE_FILE = "template.html"
  template = templateEnv.get_template(TEMPLATE_FILE)
  outputText = template.render()  # this is where to put args to the template renderer
