static void uniforms(GLuint p, const PostConstants& c)  {
    glUniform2iv(glGetUniformLocation(p,"gSize"), 1, c.size);
    glUniform2iv(glGetUniformLocation(p,"gLevelSize"), 1, c.levelSize);
    glUniform2fv(glGetUniformLocation(p,"gSourceTexel"), 1, c.sourceTexel);
    glUniform1f(glGetUniformLocation(p,"gSpread"), c.spread);
    glUniform1f(glGetUniformLocation(p,"gBloomStrength"), c.bloomStrength);
    glUniform3fv(glGetUniformLocation(p,"gWhiteBalance"), 1, c.whiteBalance);
    glUniform1f(glGetUniformLocation(p,"gSaturation"), c.saturation);
    glUniform3fv(glGetUniformLocation(p,"gLift"), 1, c.lift);
    glUniform1f(glGetUniformLocation(p,"gVignette"), c.vignette);
    glUniform3fv(glGetUniformLocation(p,"gGamma"), 1, c.gamma);
    glUniform1f(glGetUniformLocation(p,"gLutSize"), c.lutSize);
    glUniform3fv(glGetUniformLocation(p,"gGain"), 1, c.gain);
    glUniform1i(glGetUniformLocation(p,"gGrade"), c.grade);
}
